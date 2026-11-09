param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$Output,
      [string]$Cxx = 'cl',
      [ValidateRange(3,100)][int]$Samples = 9,
      [ValidateRange(4,10000000)][long]$Count = 1000000,
      [ValidateRange(1,100)][long]$Rounds = 12)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$Wood = (Resolve-Path -LiteralPath $Wood).Path
$Cxx = (Get-Command $Cxx -ErrorAction Stop).Source
$outputPath = [IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path $outputPath -Parent
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$runDirectory = Join-Path $outputDirectory ('lazy-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$extension = if ($IsWindows) { '.exe' } else { '' }
$woodExecutable = Join-Path $runDirectory "wood$extension"
$cppExecutable = Join-Path $runDirectory "cpp$extension"
$copyExecutable = Join-Path $runDirectory "wood-copy-callee$extension"
$source = Join-Path $PSScriptRoot 'lazy_pipelines.wd'
$generated = Join-Path $runDirectory 'wood.cpp'
$copySource = Join-Path $runDirectory 'wood-copy-callee.cpp'
$reference = Join-Path $PSScriptRoot 'reference_lazy_pipelines.cpp'
function Native-Compile([string]$inputSource, [string]$executable) {
    if ([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')) {
        & $Cxx /nologo /std:c++latest /EHsc /utf-8 /permissive- /O2 "/I$repositoryRoot/runtime" "/Fe:$executable" "/Fo:$executable.obj" $inputSource /link /INCREMENTAL:NO
    } else {
        & $Cxx -std=c++23 -O2 "-I$repositoryRoot/runtime" $inputSource -o $executable -pthread $(if ($IsLinux) { '-ldl' })
    }
    if ($LASTEXITCODE -ne 0) { throw "Native compilation failed: $inputSource" }
}
$compileWatch = [Diagnostics.Stopwatch]::StartNew()
& $Wood build $source -O2 --cxx $Cxx -o $woodExecutable
if ($LASTEXITCODE -ne 0) { throw 'Wood compilation failed' }
$compileWatch.Stop()
& $Wood emit-cpp $source -o $generated
if ($LASTEXITCODE -ne 0) { throw 'C++ emission failed' }
Native-Compile $reference $cppExecutable
# Controlled ablation: copying a pure callable at invocation instead of borrowing.
# Only local callable target staging with an immediately following argument is changed.
$code = [IO.File]::ReadAllText($generated)
$pattern = 'const auto& (wd_t[0-9]+ = wd_v[0-9]+; auto wd_t[0-9]+ = )'
$replacements = [regex]::Matches($code, $pattern).Count
if ($replacements -lt 1) { throw 'Cannot identify the capture-call ablation site' }
[IO.File]::WriteAllText($copySource, [regex]::Replace($code, $pattern, 'auto $1'))
Native-Compile $copySource $copyExecutable
function Expected-Checksum([string]$workload) {
    [bigint]$result = 0
    if ($workload -eq 'pipeline') {
        [bigint]$accepted = [Math]::Floor($Count / 4)
        for ($round=0; $round -lt $Rounds; $round++) {
            $result += 3 * $accepted * ($accepted - 1) / 2 * ($round % 7 + 1) + $accepted
        }
    } else {
        [bigint]$blocks = [Math]::Floor($Count / 256)
        [bigint]$rest = $Count % 256
        $result = $Rounds * ($Count * 1408 + $blocks * 32640 + $rest * ($rest - 1) / 2)
    }
    return [long]$result
}
function Measure-Executable([string]$kind, [string]$workload) {
    $executable = switch ($kind) { 'wood' { $woodExecutable } 'copy_callee' { $copyExecutable } default { $cppExecutable } }
    $mode = if ($kind -eq 'cpp_checked') { 'checked' } else { 'unchecked' }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $text = & $executable $workload $Count $Rounds $mode | Out-String
    $status = $LASTEXITCODE
    $watch.Stop()
    if ($status -ne 0) { throw "Benchmark failed ($status): $kind/$workload" }
    $data = $text | ConvertFrom-Json
    if ($data.checksum -ne (Expected-Checksum $workload) -or $data.elapsed_ns -le 0) { throw "Checksum/timer mismatch: $kind/$workload $text" }
    return [ordered]@{ elapsed_ns = [long]$data.elapsed_ns; process_ms = $watch.Elapsed.TotalMilliseconds; checksum = [long]$data.checksum }
}
$kinds = @('wood','cpp_checked','cpp_unchecked','copy_callee')
$raw = [Collections.Generic.List[object]]::new()
foreach ($workload in @('pipeline','capture')) {
    foreach ($kind in $kinds) {
        1..2 | ForEach-Object { Measure-Executable $kind $workload | Out-Null }
    }
    for ($index=0; $index -lt $Samples; $index++) {
        $order = if ($index % 2 -eq 0) { $kinds } else { @('copy_callee','cpp_unchecked','cpp_checked','wood') }
        foreach ($kind in $order) {
            $sample = Measure-Executable $kind $workload
            $raw.Add([ordered]@{ workload=$workload; implementation=$kind; sample=$index; elapsed_ns=$sample.elapsed_ns; process_ms=$sample.process_ms; checksum=$sample.checksum })
        }
    }
}
$medians = [ordered]@{}
foreach ($workload in @('pipeline','capture')) {
    $result = [ordered]@{}
    foreach ($kind in $kinds) {
        $sorted = @($raw | Where-Object { $_.workload -eq $workload -and $_.implementation -eq $kind } | Sort-Object { [long]$_['elapsed_ns'] })
        $middle = [int][Math]::Floor($sorted.Count/2)
        $median = $sorted[$middle].elapsed_ns
        if ($sorted.Count % 2 -eq 0) { $median = ($sorted[$middle-1].elapsed_ns + $median) / 2.0 }
        $result["${kind}_ns"] = $median
    }
    $result.wood_over_checked_cpp = $result.wood_ns / $result.cpp_checked_ns
    $result.wood_over_unchecked_cpp = $result.wood_ns / $result.cpp_unchecked_ns
    $result.copy_over_borrow = $result.copy_callee_ns / $result.wood_ns
    $medians[$workload] = $result
}
$report = [ordered]@{
    schema_version=1; date_utc=[DateTime]::UtcNow.ToString('o')
    workloads=@('filter/map/take/fold over integer range; captured integer multiplier','repeated concrete closure invocation with 1408-byte owned string capture')
    baselines='Straight C++ loops, checked scalar arithmetic and unchecked valid-domain arithmetic; identical results. Controlled generated-C++ callee-copy ablation.'
    warmups_per_executable=2; samples_per_executable=$Samples; count=$Count; rounds=$Rounds
    os=[Runtime.InteropServices.RuntimeInformation]::OSDescription; architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processor=$env:PROCESSOR_IDENTIFIER; compiler=$Cxx
    compiler_version=$(if ($IsWindows) { (Get-Item -LiteralPath $Cxx).VersionInfo.FileVersion } else { (& $Cxx --version | Select-Object -First 1) })
    cpp_flags='C++23 /O2 or -O2; no LTO; same compiler and default runtime as Wood -O2'
    wood_compile_ms=$compileWatch.Elapsed.TotalMilliseconds; copy_ablation_sites=$replacements
    medians=$medians; raw_samples=$raw
}
foreach ($entry in @{ compiler=$Wood; wood=$woodExecutable; cpp=$cppExecutable; copy_callee=$copyExecutable; wood_source=$source; cpp_source=$reference; generated_cpp=$generated; ablation_cpp=$copySource }.GetEnumerator()) {
    $report["$($entry.Key)_sha256"] = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash.ToLowerInvariant()
}
$report.wood_compiler_sha256 = $report.compiler_sha256
$report.host_compiler_sha256 = (Get-FileHash -LiteralPath $Cxx -Algorithm SHA256).Hash.ToLowerInvariant()
$report.runner_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$runtimeHashes = [ordered]@{}
foreach ($relative in @('include/wood/json.hpp') + @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'runtime') -Filter '*.hpp' | Sort-Object Name | ForEach-Object { 'runtime/' + $_.Name })) {
    $runtimeHashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $repositoryRoot $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$report.runtime_sources_sha256 = $runtimeHashes
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "Verified measurements: $outputPath"
$medians | ConvertTo-Json
