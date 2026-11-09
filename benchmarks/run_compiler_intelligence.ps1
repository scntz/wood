param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$BeforeWood,
      [Parameter(Mandatory)][string]$Output,
      [string]$Cxx = 'cl',
      [ValidateRange(3,100)][int]$Samples = 9,
      [ValidateRange(4,10000000)][long]$Count = 1000000,
      [ValidateRange(4,100000)][long]$PayloadCount = 10000,
      [ValidateRange(1,100)][long]$Rounds = 12)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$Wood = (Resolve-Path -LiteralPath $Wood).Path
$BeforeWood = (Resolve-Path -LiteralPath $BeforeWood).Path
$Cxx = (Get-Command $Cxx -ErrorAction Stop).Source
$outputPath = [IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path $outputPath -Parent
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$runDirectory = Join-Path $outputDirectory ('intelligence-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$extension = if ($IsWindows) { '.exe' } else { '' }
$source = Join-Path $PSScriptRoot 'compiler_intelligence.wd'
$reference = Join-Path $PSScriptRoot 'reference_compiler_intelligence.cpp'
$instrument = Join-Path $PSScriptRoot 'allocation_counter.hpp'
$executables = [ordered]@{}
$artifacts = [ordered]@{ before_compiler=$BeforeWood; compiler=$Wood; host_compiler=$Cxx; runner=$PSCommandPath; wood_source=$source; cpp_source=$reference; allocation_counter=$instrument }
function Native-Compile([string]$inputSource, [string]$executable) {
    if ([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')) {
        & $Cxx /nologo /std:c++latest /EHsc /utf-8 /permissive- /O2 "/I$repositoryRoot/runtime" "/I$PSScriptRoot" "/Fe:$executable" "/Fo:$executable.obj" $inputSource /link /INCREMENTAL:NO
    } else {
        & $Cxx -std=c++23 -O2 "-I$repositoryRoot/runtime" "-I$PSScriptRoot" $inputSource -o $executable -pthread $(if ($IsLinux) { '-ldl' })
    }
    if ($LASTEXITCODE -ne 0) { throw "Native compilation failed: $inputSource" }
}
foreach ($kind in @('before','wood','cpp')) {
    $executable = Join-Path $runDirectory "$kind$extension"
    $inputSource = $reference
    if ($kind -ne 'cpp') {
        $inputSource = Join-Path $runDirectory "$kind.cpp"
        $compiler = if ($kind -eq 'before') { $BeforeWood } else { $Wood }
        & $compiler emit-cpp $source -o $inputSource
        if ($LASTEXITCODE -ne 0) { throw "C++ emission failed: $kind" }
        # Instrument allocations without changing any generated algorithm.
        $code = [IO.File]::ReadAllText($inputSource)
        [IO.File]::WriteAllText($inputSource, "#include `"allocation_counter.hpp`"`n" + $code)
        $artifacts["${kind}_generated"] = $inputSource
    }
    Native-Compile $inputSource $executable
    $executables[$kind] = $executable
    $artifacts[$kind] = $executable
}
# Controlled ablation: retain current generated code and restore only the earlier
# general-purpose Range implementation. No workload-specific code is rewritten.
$rangePattern = '(?s)class Range \{.*?\n\};'
$beforeCode = [IO.File]::ReadAllText($artifacts.before_generated)
$currentCode = [IO.File]::ReadAllText($artifacts.wood_generated)
$oldRange = [regex]::Matches($beforeCode, $rangePattern)
$newRange = [regex]::Matches($currentCode, $rangePattern)
if ($oldRange.Count -ne 1 -or $newRange.Count -ne 1) { throw 'Expected exactly one runtime Range definition' }
$ablationSource = Join-Path $runDirectory 'range-before.cpp'
[IO.File]::WriteAllText($ablationSource, $currentCode.Replace($newRange[0].Value, $oldRange[0].Value))
$ablationExecutable = Join-Path $runDirectory "range-before$extension"
Native-Compile $ablationSource $ablationExecutable
$executables.range_before = $ablationExecutable
$artifacts.range_before = $ablationExecutable
$artifacts.range_before_generated = $ablationSource
function Expected-Checksum([string]$mode, [long]$size) {
    [bigint]$result = 0
    if ($mode -eq 'pipeline') {
        [bigint]$accepted = [Math]::Floor($size / 4)
        for ($round=0; $round -lt $Rounds; $round++) {
            $result += 3 * $accepted * ($accepted - 1) / 2 * ($round % 7 + 1) + $accepted
        }
    } elseif ($mode -eq 'capture') {
        [bigint]$blocks = [Math]::Floor($size / 256)
        [bigint]$rest = $size % 256
        $result = $Rounds * ($size * 1408 + $blocks * 32640 + $rest * ($rest - 1) / 2)
    } elseif ($mode -eq 'ranges') {
        # The stride and output residue repeat every lcm(7,17) = 119 starts.
        [bigint]$period = 0
        [bigint]$tail = 0
        for ($start=0; $start -lt 119; $start++) {
            [long]$subtotal = 0
            for ($offset=0; $offset -lt 8; $offset += $start % 7 + 1) { $subtotal += ($start + $offset) % 17 }
            $period += $subtotal
            if ($start -lt $size % 119) { $tail += $subtotal }
        }
        $result = ($period * [bigint][Math]::Floor($size / 119) + $tail) * $Rounds
    } else { $result = [bigint]$size * $Rounds * 120 }
    return [long]$result
}
function Measure-Executable([string]$kind, [string]$mode, [long]$size) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $text = & $executables[$kind] $mode $size $Rounds | Out-String
    $status = $LASTEXITCODE
    $watch.Stop()
    if ($status -ne 0) { throw "Benchmark failed ($status): $kind/$mode" }
    $data = $text | ConvertFrom-Json
    if ($data.checksum -ne (Expected-Checksum $mode $size) -or $data.elapsed_ns -le 0 -or $data.allocations -lt 0) { throw "Invalid measurement: $kind/$mode $text" }
    return [ordered]@{ elapsed_ns=[long]$data.elapsed_ns; process_ms=$watch.Elapsed.TotalMilliseconds; checksum=[long]$data.checksum; allocations=[long]$data.allocations }
}
$raw = [Collections.Generic.List[object]]::new()
foreach ($mode in @('pipeline','capture','payload','ranges')) {
    $size = if ($mode -eq 'payload') { $PayloadCount } else { $Count }
    foreach ($kind in $executables.Keys) { 1..2 | ForEach-Object { Measure-Executable $kind $mode $size | Out-Null } }
    for ($index=0; $index -lt $Samples; $index++) {
        $order = if ($index % 2 -eq 0) { @('before','wood','cpp','range_before') } else { @('range_before','cpp','wood','before') }
        foreach ($kind in $order) {
            $sample = Measure-Executable $kind $mode $size
            $raw.Add([ordered]@{ workload=$mode; implementation=$kind; sample=$index; count=$size; elapsed_ns=$sample.elapsed_ns; process_ms=$sample.process_ms; checksum=$sample.checksum; allocations=$sample.allocations })
        }
    }
}
# Process-wall frontend latency on exactly the same typed input, including startup
# and source loading; no host C++ compiler is invoked by check.
$frontendSource = Join-Path $runDirectory 'frontend.wd'
$builder = [Text.StringBuilder]::new()
for ($index=0; $index -lt 1000; $index++) { [void]$builder.Append("fn operation${index}(value: int) -> int:`n    return value + $index`n") }
[IO.File]::WriteAllText($frontendSource, $builder.ToString())
$frontend = [Collections.Generic.List[object]]::new()
for ($index=-2; $index -lt $Samples; $index++) {
    $order = if ($index % 2 -eq 0) { @('before','wood') } else { @('wood','before') }
    foreach ($kind in $order) {
        $compiler = if ($kind -eq 'before') { $BeforeWood } else { $Wood }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        & $compiler check $frontendSource
        $status = $LASTEXITCODE
        $watch.Stop()
        if ($status -ne 0) { throw "Frontend failed: $kind" }
        if ($index -ge 0) { $frontend.Add([ordered]@{ implementation=$kind; sample=$index; elapsed_ms=$watch.Elapsed.TotalMilliseconds }) }
    }
}
$artifacts.frontend_source = $frontendSource
$medians = [ordered]@{}
foreach ($mode in @('pipeline','capture','payload','ranges')) {
    $result = [ordered]@{}
    foreach ($kind in $executables.Keys) {
        $sorted = @($raw | Where-Object { $_.workload -eq $mode -and $_.implementation -eq $kind } | Sort-Object { [long]$_['elapsed_ns'] })
        $middle = [int][Math]::Floor($sorted.Count/2)
        $median = $sorted[$middle].elapsed_ns
        if ($sorted.Count % 2 -eq 0) { $median = ($sorted[$middle-1].elapsed_ns + $median) / 2.0 }
        for ($position=1; $position -lt $sorted.Count; $position++) {
            if ($sorted[$position-1].elapsed_ns -gt $sorted[$position].elapsed_ns) { throw 'Timing sort invariant failed' }
        }
        if (@($sorted.allocations | Select-Object -Unique).Count -ne 1) { throw "Unstable allocation count: $mode/$kind" }
        $result[$kind] = [ordered]@{ median_ns=$median; minimum_ns=$sorted[0].elapsed_ns; maximum_ns=$sorted[-1].elapsed_ns; allocations=$sorted[0].allocations }
    }
    $result.before_over_wood = $result.before.median_ns / $result.wood.median_ns
    $result.wood_over_cpp = $result.wood.median_ns / $result.cpp.median_ns
    $result.old_range_over_wood = $result.range_before.median_ns / $result.wood.median_ns
    $medians[$mode] = $result
}
$hashes = [ordered]@{}
foreach ($name in $artifacts.Keys) { $hashes[$name] = (Get-FileHash -LiteralPath $artifacts[$name] -Algorithm SHA256).Hash.ToLowerInvariant() }
$runtimeHashes = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'runtime') -Filter '*.hpp' | Sort-Object Name) { $runtimeHashes[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$report = [ordered]@{
    schema_version=1; date_utc=[DateTime]::UtcNow.ToString('o'); samples=$Samples; warmups=2; count=$Count; payload_count=$PayloadCount; rounds=$Rounds
    baseline='Optimized straight C++ loops, checked int64 payload arithmetic; identical validated domains, owned capture lifetime, immutable string input and results. No unchecked baseline. range_before changes only the current generated runtime Range definition to the earlier one.'
    instrumentation='Global new/new[] including aligned/nothrow forms counted only during algorithm; input setup and JSON output excluded. Instrumented timing includes allocation counter overhead in all implementations.'
    os=[Runtime.InteropServices.RuntimeInformation]::OSDescription; architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString(); processor=$env:PROCESSOR_IDENTIFIER
    processor_model=$(if ($IsWindows) { ([Microsoft.Win32.Registry]::GetValue('HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0','ProcessorNameString','unknown')).Trim() } else { $env:PROCESSOR_IDENTIFIER })
    logical_processors=[Environment]::ProcessorCount
    host_compiler=$Cxx; compiler_version=$(if ($IsWindows) { (Get-Item -LiteralPath $Cxx).VersionInfo.FileVersion } else { (& $Cxx --version | Select-Object -First 1) })
    flags='C++23 /O2 or -O2; no LTO; same compiler/default runtime for all programs'; medians=$medians; raw_samples=$raw; frontend_process_samples=$frontend
    artifacts=$artifacts; sha256=$hashes; runtime_sha256=$runtimeHashes
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "Verified measurements: $outputPath"
$medians | ConvertTo-Json -Depth 5
