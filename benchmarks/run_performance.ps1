param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$BeforeWood,
      [Parameter(Mandatory)][string]$Output,
      [string]$Cxx = 'cl',
      [ValidateRange(3,31)][int]$Samples = 9,
      [ValidateRange(1,100)][int]$Rounds = 12,
      [ValidateRange(-1,62)][int]$Cpu = -1,
      [switch]$TimingOnly,
      [switch]$ProfileFrontend,
      [string]$LibraryDirectory,
      [string]$BeforeLibraryDirectory)
$ErrorActionPreference = 'Stop'
$affinityProcess = $null
$restoreAffinity = $false
try {
if ($Cpu -ge 0) {
    if (!$IsWindows) { throw '-Cpu currently supports the Windows measurement host only' }
    $affinityProcess = [Diagnostics.Process]::GetCurrentProcess()
    $originalAffinity = $affinityProcess.ProcessorAffinity
    $restoreAffinity = $true
    $mask = [long]1 -shl $Cpu
    if (($originalAffinity.ToInt64() -band $mask) -eq 0) { throw 'Selected CPU is unavailable' }
    $affinityProcess.ProcessorAffinity = [IntPtr]$mask
}
$root = Split-Path $PSScriptRoot -Parent
$Wood = (Resolve-Path -LiteralPath $Wood).Path
$BeforeWood = (Resolve-Path -LiteralPath $BeforeWood).Path
$Cxx = (Get-Command $Cxx).Source
$outputPath = [IO.Path]::GetFullPath($Output)
$directory = Join-Path (Split-Path $outputPath -Parent) ('performance-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory) | Out-Null
$extension = if ($IsWindows) { '.exe' } else { '' }
$source = Join-Path $PSScriptRoot 'performance.wd'
$reference = Join-Path $PSScriptRoot 'reference_performance.cpp'
$artifacts = [ordered]@{before_compiler=$BeforeWood; compiler=$Wood; host_compiler=$Cxx; runner=$PSCommandPath; wood_source=$source; cpp_source=$reference; instrumentation=(Join-Path $PSScriptRoot 'native_memory.hpp')}
$counterFlag = if ($TimingOnly) { 0 } else { 1 }
$executables = [ordered]@{}
foreach ($kind in @('before','wood','cpp','range_before')) {
    $inputSource = $reference
    if ($kind -eq 'range_before') {
        $inputSource = Join-Path $directory 'range-before.cpp'
        $beforeCode = [IO.File]::ReadAllText($artifacts.before_generated)
        $currentCode = [IO.File]::ReadAllText($artifacts.wood_generated)
        $pattern = '(?s)class Range \{.*?\n\};'
        $oldRange = [regex]::Matches($beforeCode, $pattern)
        $newRange = [regex]::Matches($currentCode, $pattern)
        if ($oldRange.Count -ne 1 -or $newRange.Count -ne 1) { throw 'Expected exactly one runtime Range class' }
        [IO.File]::WriteAllText($inputSource, $currentCode.Replace($newRange[0].Value, $oldRange[0].Value))
    } elseif ($kind -ne 'cpp') {
        $inputSource = Join-Path $directory "$kind.cpp"
        $compiler = if ($kind -eq 'before') { $BeforeWood } else { $Wood }
        & $compiler emit-cpp $source -o $inputSource
        if ($LASTEXITCODE -ne 0) { throw "Emission failed: $kind" }
        [IO.File]::WriteAllText($inputSource, "#include `"native_memory.hpp`"`n" + [IO.File]::ReadAllText($inputSource))
    }
    $exe = Join-Path $directory "$kind$extension"
    if ([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')) {
        $log = & $Cxx /nologo /std:c++latest /O2 /EHsc /utf-8 /permissive- "/DWOOD_BENCH_MEMORY_COUNTERS=$counterFlag" "/I$root/runtime" "/I$PSScriptRoot" "/Fe:$exe" "/Fo:$exe.obj" $inputSource /link /INCREMENTAL:NO 2>&1 | Out-String
    } else {
        $log = & $Cxx -std=c++23 -O2 "-DWOOD_BENCH_MEMORY_COUNTERS=$counterFlag" "-I$root/runtime" "-I$PSScriptRoot" $inputSource -o $exe -pthread $(if($IsLinux){'-ldl'}) $(if($IsWindows){'-lpsapi'}) 2>&1 | Out-String
    }
    $status = $LASTEXITCODE
    $logPath = Join-Path $directory "$kind-build.log"
    [IO.File]::WriteAllText($logPath, $log)
    if ($status -ne 0) { throw "Native build failed: $kind $log" }
    $executables[$kind] = $exe
    $artifacts["${kind}_generated"] = $inputSource
    $artifacts["${kind}_executable"] = $exe
    $artifacts["${kind}_build_log"] = $logPath
}
function Residue-Sum([long]$count, [long]$period) {
    [long]$blocks = [Math]::Floor($count / $period)
    [long]$rest = $count % $period
    return $blocks * $period * ($period - 1) / 2 + $rest * ($rest - 1) / 2
}
function Digits-Sum([long]$count) {
    [long]$result = 1 # zero
    [long]$start = 1
    for ($digits=1; $start -lt $count; $digits++) {
        $result += ([Math]::Min($count, $start*10) - $start) * $digits
        $start *= 10
    }
    return $result
}
function Expected([string]$mode, [long]$count) {
    switch ($mode) {
        'scan' { return $Rounds * (144*$count + (Digits-Sum $count) + (Residue-Sum $count 10)) }
        'concat' { return $Rounds * (147*$count + 2*(Digits-Sum $count)) }
        'payload' { return $Rounds * (135*$count + (Digits-Sum $count)) }
        'payload_direct' { return $Rounds * (135*$count + (Digits-Sum $count)) }
        'payload_eager' { return $Rounds * (135*$count + (Digits-Sum $count)) }
        'lists' { return (Residue-Sum $count 97) + $Rounds*$count }
        'dictionary' { return $Rounds*(Residue-Sum $count 97) }
        'validation' { return $Rounds*(Residue-Sum $count 97) }
        'arrays' { return $Rounds*(0.1875*(Residue-Sum $count 1024) + 0.0625*(Residue-Sum $count 17)) }
        'capture' { return $Rounds*(1408*$count + (Residue-Sum $count 256)) }
        'pipeline' {
            [long]$accepted = [Math]::Floor($count/4)
            [long]$result = 0
            for ($round=0; $round -lt $Rounds; $round++) {
                $result += 3*$accepted*($accepted-1)/2*($round%7+1)+$accepted
            }
            return $result
        }
        'ranges' {
            [long]$period = 0
            [long]$tail = 0
            for ($start=0; $start -lt 119; $start++) {
                [long]$subtotal = 0
                for ($offset=0; $offset -lt 8; $offset += $start%7+1) { $subtotal += ($start+$offset)%17 }
                $period += $subtotal
                if ($start -lt $count%119) { $tail += $subtotal }
            }
            return $Rounds*([long][Math]::Floor($count/119)*$period+$tail)
        }
        default { throw "Unknown workload: $mode" }
    }
}
$sizes = [ordered]@{scan=20000; concat=20000; payload=20000; payload_direct=20000; payload_eager=20000; lists=200000; dictionary=10000; arrays=200000; validation=200000; pipeline=1000000; capture=1000000; ranges=1000000}
function Measure-Workload([string]$kind, [string]$mode, [long]$count) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $text = & $executables[$kind] $mode $count $Rounds | Out-String
    $status = $LASTEXITCODE
    $watch.Stop()
    if ($status -ne 0) { throw "Execution failed: $kind/$mode ($status) $text" }
    $data = $text | ConvertFrom-Json
    if ($Cpu -ge 0 -and $data.cpu_mask -ne ([long]1 -shl $Cpu)) { throw 'Child CPU affinity did not match requested CPU' }
    $memoryValid = if ($TimingOnly) { $data.allocations -eq -1 -and $data.allocated_bytes -eq -1 -and $data.peak_bytes -eq -1 } else { $data.allocations -ge 0 -and $data.allocated_bytes -ge 0 -and $data.peak_bytes -ge 0 }
    if ($data.checksum -ne (Expected $mode $count) -or $data.elapsed_ns -le 0 -or !$memoryValid -or $data.process_peak_bytes -le 0) { throw "Incorrect measurement: $kind/$mode $text" }
    return [ordered]@{elapsed_ns=[long]$data.elapsed_ns; process_ms=$watch.Elapsed.TotalMilliseconds; checksum=$data.checksum; allocations=[long]$data.allocations; allocated_bytes=[long]$data.allocated_bytes; peak_bytes=[long]$data.peak_bytes; process_peak_bytes=[long]$data.process_peak_bytes; cpu_mask=[long]$data.cpu_mask}
}
$raw = [Collections.Generic.List[object]]::new()
foreach ($mode in $sizes.Keys) {
    foreach ($kind in $executables.Keys) { 1..2 | ForEach-Object { Measure-Workload $kind $mode $sizes[$mode] | Out-Null } }
    for ($index=0; $index -lt $Samples; $index++) {
        $kinds = @($executables.Keys)
        # Rotate and reverse order to distribute first/last positions.
        $order = @(for ($position=0; $position -lt $kinds.Count; $position++) { $kinds[($index+$position)%$kinds.Count] })
        if ($index%2) { [array]::Reverse($order) }
        foreach ($kind in $order) {
            $data = Measure-Workload $kind $mode $sizes[$mode]
            $raw.Add([ordered]@{workload=$mode; implementation=$kind; sample=$index; count=$sizes[$mode]; measurement=$data})
        }
    }
}
# Process-wall compiler checks and C++ emission, including startup and I/O.
$frontendSource = Join-Path $directory 'frontend.wd'
$builder = [Text.StringBuilder]::new()
for ($index=0; $index -lt 1000; $index++) {
    if ($index -lt 500) { [void]$builder.Append("fn operation${index}(value: int) -> int:`n    return value + $index`n") }
    else { [void]$builder.Append("fn operation${index}(value: tuple[str,list[int],dict[str,int]]) -> tuple[str,list[int],dict[str,int]]:`n    return value`n") }
}
[IO.File]::WriteAllText($frontendSource, $builder.ToString())
$artifacts.frontend_source = $frontendSource
$frontend = [Collections.Generic.List[object]]::new()
foreach ($mode in @('check','emit-cpp')) {
    for ($index=-2; $index -lt $Samples; $index++) {
        $order = if ($index%2 -eq 0) { @('before','wood') } else { @('wood','before') }
        foreach ($kind in $order) {
            $compiler = if ($kind -eq 'before') { $BeforeWood } else { $Wood }
            $watch = [Diagnostics.Stopwatch]::StartNew()
            if ($mode -eq 'check') { & $compiler check $frontendSource }
            else { & $compiler emit-cpp $frontendSource -o (Join-Path $directory 'frontend.cpp') }
            $status = $LASTEXITCODE
            $watch.Stop()
            if ($status -ne 0) { throw "Frontend failed: $kind/$mode" }
            if ($index -ge 0) { $frontend.Add([ordered]@{mode=$mode; implementation=$kind; sample=$index; elapsed_ms=$watch.Elapsed.TotalMilliseconds}) }
        }
    }
}
$frontendStages = [Collections.Generic.List[object]]::new()
if ($ProfileFrontend) {
    if (!$LibraryDirectory) { $LibraryDirectory = Split-Path $Wood -Parent }
    if (!$BeforeLibraryDirectory) { $BeforeLibraryDirectory = Split-Path $BeforeWood -Parent }
    $profileSource = Join-Path $PSScriptRoot 'frontend_profile.cpp'
    $artifacts.frontend_profile_source = $profileSource
    $profiles = [ordered]@{}
    foreach ($kind in @('before','wood')) {
        $libraryPath = if ($kind -eq 'before') { $BeforeLibraryDirectory } else { $LibraryDirectory }
        $exe = Join-Path $directory "frontend-$kind$extension"
        if ([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')) {
            $compilerLibrary = Join-Path $libraryPath 'wood_compiler.lib'
            $supportLibrary = Join-Path $libraryPath 'wood_support.lib'
            $log = & $Cxx /nologo /std:c++latest /O2 /MD /EHsc /utf-8 /permissive- "/I$root/include" "/I$PSScriptRoot" "/Fe:$exe" "/Fo:$exe.obj" $profileSource $compilerLibrary $supportLibrary /link /INCREMENTAL:NO /STACK:8388608 2>&1 | Out-String
        } else {
            $compilerLibrary = Join-Path $libraryPath 'libwood_compiler.a'
            $supportLibrary = Join-Path $libraryPath 'libwood_support.a'
            $log = & $Cxx -std=c++23 -O2 "-I$root/include" "-I$PSScriptRoot" $profileSource $compilerLibrary $supportLibrary -o $exe -pthread $(if($IsLinux){'-ldl'}) $(if($IsWindows){'-lpsapi'}) 2>&1 | Out-String
        }
        $status = $LASTEXITCODE
        $logPath = Join-Path $directory "frontend-$kind-build.log"
        [IO.File]::WriteAllText($logPath, $log)
        if ($status -ne 0) { throw "Frontend profiling build failed: $kind $log" }
        $profiles[$kind] = $exe
        $artifacts["${kind}_frontend_executable"] = $exe
        $artifacts["${kind}_compiler_library"] = $compilerLibrary
        $artifacts["${kind}_support_library"] = $supportLibrary
        $artifacts["${kind}_frontend_build_log"] = $logPath
    }
    foreach ($phase in @('lex','parse','analyze','generate','all')) {
        for ($index=-2; $index -lt $Samples; $index++) {
            $order = if ($index%2 -eq 0) { @('before','wood') } else { @('wood','before') }
            foreach ($kind in $order) {
                $text = & $profiles[$kind] $phase $frontendSource | Out-String
                if ($LASTEXITCODE -ne 0) { throw "Frontend profiling failed: $kind/$phase" }
                $data = $text | ConvertFrom-Json
                if ($Cpu -ge 0 -and $data.cpu_mask -ne ([long]1 -shl $Cpu)) { throw 'Frontend child CPU affinity mismatch' }
                if ($data.elapsed_ns -le 0 -or $data.allocations -le 0 -or $data.process_peak_bytes -le 0 -or (($phase -in @('all','generate')) -and $data.output_bytes -le 0)) { throw "Invalid frontend profile: $kind/$phase $text" }
                if ($index -ge 0) { $frontendStages.Add([ordered]@{phase=$phase; implementation=$kind; sample=$index; measurement=$data}) }
            }
        }
    }
}
$summary = [ordered]@{}
foreach ($mode in $sizes.Keys) {
    $summary[$mode] = [ordered]@{}
    foreach ($kind in $executables.Keys) {
        $data = @($raw | Where-Object { $_.workload -eq $mode -and $_.implementation -eq $kind } | ForEach-Object { $_.measurement })
        $entry = [ordered]@{}
        foreach ($metric in @('elapsed_ns','allocations','allocated_bytes','peak_bytes','process_peak_bytes')) {
            $sorted = @($data | ForEach-Object { [long]$_[$metric] } | Sort-Object)
            for ($j=1; $j -lt $sorted.Count; $j++) { if ($sorted[$j-1] -gt $sorted[$j]) { throw 'Numeric sort invariant failed' } }
            $middle = [int][Math]::Floor($sorted.Count/2)
            $median = $sorted[$middle]
            if ($sorted.Count%2 -eq 0) { $median = ($sorted[$middle-1]+$median)/2.0 }
            $entry[$metric] = [ordered]@{median=$median; minimum=$sorted[0]; maximum=$sorted[-1]}
            if ($metric -in @('allocations','allocated_bytes','peak_bytes') -and $sorted[0] -ne $sorted[-1]) { throw "Unstable allocation data: $mode/$kind/$metric" }
        }
        $summary[$mode][$kind] = $entry
    }
}
# A complete, unchanged Wood application; only its entry point is wrapped for
# measurement. No algorithm-specific generated code is substituted.
$applicationSource = Join-Path $root 'examples/delimited_log_report.wd'
$wrapper = Join-Path $PSScriptRoot 'application_wrapper.hpp'
$artifacts.application_source = $applicationSource
$artifacts.application_wrapper = $wrapper
$applicationExecutables = [ordered]@{}
foreach ($kind in @('before','wood')) {
    $compiler = if ($kind -eq 'before') { $BeforeWood } else { $Wood }
    $inputSource = Join-Path $directory "application-$kind.cpp"
    & $compiler emit-cpp $applicationSource -o $inputSource --diagnostics=json
    if ($LASTEXITCODE -ne 0) { throw "Application emission failed: $kind" }
    $entryName = if ($IsWindows) { 'wmain' } else { 'main' }
    $code = "#include `"native_memory.hpp`"`n#define $entryName wood_bench_application_main`n" + [IO.File]::ReadAllText($inputSource) + "`n#include `"application_wrapper.hpp`"`n"
    [IO.File]::WriteAllText($inputSource, $code)
    $exe = Join-Path $directory "application-$kind$extension"
    if ([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')) {
        $log = & $Cxx /nologo /std:c++latest /O2 /EHsc /utf-8 /permissive- "/DWOOD_BENCH_MEMORY_COUNTERS=$counterFlag" "/I$PSScriptRoot" "/Fe:$exe" "/Fo:$exe.obj" $inputSource /link /INCREMENTAL:NO 2>&1 | Out-String
    } else {
        $log = & $Cxx -std=c++23 -O2 "-DWOOD_BENCH_MEMORY_COUNTERS=$counterFlag" "-I$PSScriptRoot" $inputSource -o $exe -pthread $(if($IsLinux){'-ldl'}) $(if($IsWindows){'-lpsapi'}) 2>&1 | Out-String
    }
    $status = $LASTEXITCODE
    $logPath = Join-Path $directory "application-$kind-build.log"
    [IO.File]::WriteAllText($logPath, $log)
    if ($status -ne 0) { throw "Application compilation failed: $kind $log" }
    $applicationExecutables[$kind] = $exe
    $artifacts["${kind}_application_generated"] = $inputSource
    $artifacts["${kind}_application_executable"] = $exe
    $artifacts["${kind}_application_build_log"] = $logPath
}
$applicationInput = Join-Path $directory 'events.log'
$messageBody = 'native text payload ' * 8
$writer = [IO.StreamWriter]::new($applicationInput, $false, [Text.UTF8Encoding]::new($false))
try {
    for ($index=0; $index -lt 20000; $index++) {
        $level = @('INFO','WARN','ERROR')[$index%3]
        $writer.WriteLine("$level|mesh$($index%13)|$messageBody$index")
    }
} finally { $writer.Dispose() }
$artifacts.application_input = $applicationInput
$applicationSamples = [Collections.Generic.List[object]]::new()
for ($index=-2; $index -lt $Samples; $index++) {
    $order = if ($index%2 -eq 0) { @('before','wood') } else { @('wood','before') }
    foreach ($kind in $order) {
        $stderr = Join-Path $directory 'application-metrics.json'
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $text = & $applicationExecutables[$kind] $applicationInput 2> $stderr | Out-String
        $status = $LASTEXITCODE
        $watch.Stop()
        if ($status -ne 0) { throw "Application execution failed: $kind $(Get-Content -LiteralPath $stderr -Raw)" }
        $summaryData = $text | ConvertFrom-Json
        if ($summaryData.records -ne 20000 -or $summaryData.message_bytes -ne (20000*$messageBody.Length + (Digits-Sum 20000)) -or $summaryData.levels.INFO -ne 6667 -or $summaryData.levels.WARN -ne 6667 -or $summaryData.levels.ERROR -ne 6666 -or $summaryData.preview[0] -ne "[1] INFO mesh0: ${messageBody}0") { throw "Invalid application summary: $kind $text" }
        for ($category=0; $category -lt 13; $category++) {
            $expectedCount = 1538 + [int]($category -lt 6)
            if ($summaryData.categories."mesh$category" -ne $expectedCount) { throw 'Invalid application category count' }
        }
        $data = Get-Content -LiteralPath $stderr -Raw | ConvertFrom-Json
        if ($Cpu -ge 0 -and $data.cpu_mask -ne ([long]1 -shl $Cpu)) { throw 'Application child CPU affinity mismatch' }
        if ($data.elapsed_ns -le 0 -or (!$TimingOnly -and $data.allocations -le 0) -or ($TimingOnly -and $data.allocations -ne -1) -or $data.process_peak_bytes -le 0) { throw "Invalid application metrics: $kind" }
        if ($index -ge 0) { $applicationSamples.Add([ordered]@{implementation=$kind; sample=$index; measurement=$data; process_ms=$watch.Elapsed.TotalMilliseconds}) }
    }
}
$hashes = [ordered]@{}
foreach ($name in $artifacts.Keys) { $hashes[$name] = (Get-FileHash -LiteralPath $artifacts[$name] -Algorithm SHA256).Hash.ToLowerInvariant() }
$runtime = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'runtime') -Filter '*.hpp' | Sort-Object Name) { $runtime[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$report = [ordered]@{
    schema_version=1; date_utc=[DateTime]::UtcNow.ToString('o'); samples=$Samples; warmups=2; rounds=$Rounds; sizes=$sizes; implementations=@($executables.Keys); allocation_metrics_available=(!$TimingOnly); cpu_logical_processor=$Cpu
    flags='C++23 /O2 or -O2; same host compiler and default runtime; no LTO, fast math or CPU-specific ISA'
    methodology='Rotated/reversed process order. Internal steady-clock algorithm times and process wall times. Instrumented mode: global-new counting including aligned/nothrow allocations, requested bytes and peak tracked live bytes; kernel setup/output excluded; counter overhead included. TimingOnly mode: ordinary allocator, no overrides/counters, allocation/byte fields -1 mean unavailable. Process peak working set/RSS includes setup/executable. Complete application measures I/O/setup/report too. Optional Cpu pins the Windows runner; every measured child verifies its inherited affinity mask. No fixed clock/power policy or OS isolation. range_before changes only the current embedded Range class to its before definition.'
    baseline='Straight checked C++ loops, scoped immutable string borrows, owned formatted output, checked int64 arithmetic, checked collection accesses, balanced-tree dictionary. Fused numeric C++ loop with shape validation, matching element arithmetic and compensated summation. Not an unchecked baseline or a language-wide parity claim.'
    os=[Runtime.InteropServices.RuntimeInformation]::OSDescription; architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processor_model=$(if($IsWindows){([Microsoft.Win32.Registry]::GetValue('HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0','ProcessorNameString','unknown')).Trim()}else{$env:PROCESSOR_IDENTIFIER})
    logical_processors=[Environment]::ProcessorCount; host_compiler=$Cxx; compiler_version=$(if($IsWindows){(Get-Item -LiteralPath $Cxx).VersionInfo.FileVersion}else{(& $Cxx --version | Select-Object -First 1)})
    summary=$summary; raw_samples=$raw; frontend_process_samples=$frontend; frontend_stage_samples=$frontendStages; application_samples=$applicationSamples; artifacts=$artifacts; sha256=$hashes; runtime_sha256=$runtime
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "Verified measurements: $outputPath"
foreach ($mode in $sizes.Keys) { Write-Output "$mode before=$($summary[$mode].before.elapsed_ns.median) wood=$($summary[$mode].wood.elapsed_ns.median) cpp=$($summary[$mode].cpp.elapsed_ns.median) ns; allocations=$($summary[$mode].before.allocations.median)/$($summary[$mode].wood.allocations.median)/$($summary[$mode].cpp.allocations.median)" }
} finally {
    if ($restoreAffinity) { $affinityProcess.ProcessorAffinity = $originalAffinity }
    if ($null -ne $affinityProcess) { $affinityProcess.Dispose() }
}
