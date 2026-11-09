param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$Cpp,
      [Parameter(Mandatory)][string]$Output,
      [ValidateRange(3,100)][int]$Samples = 9)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$Wood = (Resolve-Path -LiteralPath $Wood).Path
$Cpp = (Resolve-Path -LiteralPath $Cpp).Path
$outputPath = [IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path $outputPath -Parent
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$woodExecutable = Join-Path $outputDirectory ('wood-backend-' + [Guid]::NewGuid().ToString('N') + $(if ($IsWindows) { '.exe' } else { '' }))
$compileWatch = [Diagnostics.Stopwatch]::StartNew()
& $Wood build (Join-Path $PSScriptRoot 'backend.wd') -O2 -o $woodExecutable
$compileWatch.Stop()
if ($LASTEXITCODE -ne 0) { throw 'Wood benchmark compilation failed' }
function Measure-Executable([string]$executable) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $text = & $executable | Out-String
    $status = $LASTEXITCODE
    $watch.Stop()
    if ($status -ne 0) { throw "Benchmark failed ($status): $executable" }
    $data = $text | ConvertFrom-Json
    if ($data.rows -ne 100000 -or $data.checksum -ne 5000050000 -or $data.elapsed_ns -lt 0) { throw 'Benchmark checksum/timer mismatch' }
    return [ordered]@{ elapsed_ns = [long]$data.elapsed_ns; process_ms = $watch.Elapsed.TotalMilliseconds }
}
foreach ($executable in @($woodExecutable,$Cpp)) {
    Measure-Executable $executable | Out-Null
    Measure-Executable $executable | Out-Null
}
$raw = [Collections.Generic.List[object]]::new()
for ($index=0; $index -lt $Samples; $index++) {
    $order = if ($index % 2 -eq 0) { @('wood','cpp') } else { @('cpp','wood') }
    foreach ($kind in $order) {
        $executable = if ($kind -eq 'wood') { $woodExecutable } else { $Cpp }
        $sample = Measure-Executable $executable
        $raw.Add([ordered]@{ implementation=$kind; sample=$index; elapsed_ns=$sample.elapsed_ns; process_ms=$sample.process_ms })
    }
}
$report = [ordered]@{
    schema_version=1; date_utc=[DateTime]::UtcNow.ToString('o'); workload='100000 bound inserts into in-memory SQLite, one transaction, count/sum verification'
    baseline='C++ using identical Wood checked SQLite primitives; not a fastest-C++ or independent SQLite implementation'
    warmups_per_executable=2; samples_per_executable=$Samples; rows=100000; checksum=5000050000
    os=[Runtime.InteropServices.RuntimeInformation]::OSDescription; architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    processor=$env:PROCESSOR_IDENTIFIER; toolchain='CMake Release C++23 reference; Wood -O2; use the same host compiler'
    wood_compile_ms=$compileWatch.Elapsed.TotalMilliseconds
    wood_binary_sha256=(Get-FileHash -LiteralPath $woodExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    compiler_sha256=(Get-FileHash -LiteralPath $Wood -Algorithm SHA256).Hash.ToLowerInvariant()
    cpp_binary_sha256=(Get-FileHash -LiteralPath $Cpp -Algorithm SHA256).Hash.ToLowerInvariant()
    wood_source_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'backend.wd') -Algorithm SHA256).Hash.ToLowerInvariant()
    cpp_source_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'reference_backend.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()
    raw_samples=$raw
}
foreach ($kind in @('wood','cpp')) {
    $sorted = @($raw | Where-Object { $_.implementation -eq $kind } | Sort-Object elapsed_ns)
    $middle = [int][Math]::Floor($sorted.Count/2)
    $median = $sorted[$middle].elapsed_ns
    if ($sorted.Count % 2 -eq 0) { $median = ($sorted[$middle-1].elapsed_ns + $median) / 2.0 }
    $report["${kind}_median_ns"] = $median
}
$report.wood_over_cpp_median = $report.wood_median_ns / $report.cpp_median_ns
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "Verified backend measurement saved: $outputPath"
