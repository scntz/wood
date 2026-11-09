# Build measurement-only wrappers; run from an x64 Visual Studio developer shell.
# No compiler/runtime source is changed. Use bootstrap_compiler.py --instrumented
# on the three resulting executables, separately from ordinary timing runs.
param(
    [Parameter(Mandatory=$true)][string]$PreviousSource,
    [Parameter(Mandatory=$true)][string]$CompilerSource,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$previous = (Resolve-Path -LiteralPath $PreviousSource).Path
$current = (Resolve-Path -LiteralPath $CompilerSource).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Measurement output directory already exists' }
Get-Command cl.exe -ErrorAction Stop | Out-Null
$output = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$sources = [ordered]@{previous=$previous;wood=$current;cpp=(Join-Path $root 'src/main.cpp')}
$manifest = [ordered]@{format=1;method='existing native_memory.hpp counts all global-new requests during application entry; instrumented executables are separate from ordinary timings';sources=@{}}
foreach ($name in $sources.Keys) {
    $source = $sources[$name]
    $counter = (Join-Path $PSScriptRoot 'native_memory.hpp').Replace('\','/')
    $wrapper = (Join-Path $PSScriptRoot 'application_wrapper.hpp').Replace('\','/')
    $unit = "#include `"$counter`"`n#define main wood_bench_application_main`n#define wmain wood_bench_application_main`n#include `"$($source.Replace('\','/'))`"`n"
    if ($name -eq 'cpp') {
        # Production wood links this driver component separately from its two
        # libraries. Include it in this measurement-only translation unit.
        $unit += "#include `"$((Join-Path $root 'src/process.cpp').Replace('\','/'))`"`n"
    }
    $unit += "#include `"$wrapper`"`n"
    $path = Join-Path $output ($name+'.cpp')
    [IO.File]::WriteAllText($path,$unit)
    $binary = Join-Path $output ($name+'.exe')
    $flags = @('/nologo','/std:c++latest','/O2','/EHsc','/utf-8','/permissive-','/bigobj','/MD',('/Fo:'+(Join-Path $output ($name+'.obj'))),('/Fe:'+$binary))
    $libraries = @()
    if ($name -eq 'cpp') {
        $flags += @('/Ob2','/DNDEBUG',('/I'+(Join-Path $root 'include')),('/I'+(Join-Path $build 'generated')))
        $libraries = @((Join-Path $build 'Release/wood_compiler.lib'),(Join-Path $build 'Release/wood_support.lib'))
    }
    & cl.exe @flags $path @libraries /link /INCREMENTAL:NO /STACK:8388608 2>&1 | Tee-Object -FilePath (Join-Path $output ($name+'-build.log'))
    if ($LASTEXITCODE -ne 0) { throw "Instrumented $name compilation failed: $LASTEXITCODE" }
    $manifest.sources[$name] = @{source=$source;source_sha256=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant();binary=$binary;binary_sha256=(Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant();flags=$flags;libraries=$libraries}
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $output 'manifest.json')
