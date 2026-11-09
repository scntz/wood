param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
# Windows evidence that a bundle really uses only itself, on a machine that may have Python
# installed. It inspects what the running process has loaded, rather than what it reports.
if (Test-Path -LiteralPath $Work) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory $Work | Out-Null
$Wood = (Resolve-Path -LiteralPath $Wood).Path
$env:WOOD_CACHE_DIR = Join-Path $Work 'cache'
$system = Join-Path $env:SystemRoot 'System32'
$env:PATH = "$system;$env:SystemRoot"
Push-Location $Work
try {
    & $Wood init app | Out-Null
    if ($LASTEXITCODE) { throw 'init failed' }
    Push-Location app
    & $Wood add python@numpy | Out-Null
    if ($LASTEXITCODE) { throw 'add failed' }
    $source = @'
import python@numpy as np
import std.python as py

sys = py.import_module("sys")
print(py.text(py.attr(sys, "executable")))
print(py.text(py.attr(sys, "prefix")))
print(py.text(py.attr(sys, "base_prefix")))
print(py.text(py.attr(sys, "path")))
print(py.text(np.attr("__file__")))
with np.call("array", [py.value([1.0, 2.0, 3.0])]) as values:
    print(py.to_float(np.call("sum", [values])))
print("READY")
read_line()
print("DONE")
'@
    Set-Content -LiteralPath 'src/main.wd' -Value $source
    & $Wood build --bundle-python -o (Join-Path $Work 'bundle') | Out-Null
    if ($LASTEXITCODE) { throw 'bundle failed' }
    Pop-Location
    $target = Join-Path $Work 'moved here'
    Move-Item -LiteralPath (Join-Path $Work 'bundle') -Destination $target
    # Nothing built for the project remains to fall back on.
    Remove-Item -LiteralPath $env:WOOD_CACHE_DIR, (Join-Path $Work 'app') -Recurse -Force
    Remove-Item Env:WOOD_CACHE_DIR
    $start = [Diagnostics.ProcessStartInfo]::new((Join-Path $target 'app.exe'))
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($name in 'PYTHONHOME', 'PYTHONPATH', 'VIRTUAL_ENV', 'WOOD_PYTHON') { [void]$start.Environment.Remove($name) }
    $process = [Diagnostics.Process]::Start($start)
    $lines = [Collections.Generic.List[string]]::new()
    while ($true) {
        $line = $process.StandardOutput.ReadLine()
        if ($null -eq $line) { throw "The application ended early: $($process.StandardError.ReadToEnd())" }
        if ($line -eq 'READY') { break }
        $lines.Add($line)
    }
    $checks = 0
    function Inside([string]$text, [string]$what) {
        if (-not $text.Replace('/', '\').StartsWith($target, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$what is outside the bundle: $text"
        }
        $script:checks++
    }
    Inside $lines[0] 'sys.executable'
    Inside $lines[1] 'sys.prefix'
    Inside $lines[2] 'sys.base_prefix'
    Inside $lines[4] 'numpy.__file__'
    # Python prints the list with doubled backslashes.
    foreach ($entry in [regex]::Matches($lines[3], "'([^']+)'")) { Inside $entry.Groups[1].Value.Replace('\\', '\') 'a sys.path entry' }
    if ($lines[5] -ne '6.0') { throw "Wrong result: $($lines[5])" }
    # What is actually mapped into the process: every Python-related module comes from the bundle.
    $process.Refresh()
    $modules = $process.Modules | ForEach-Object { $_.FileName }
    $python = $modules | Where-Object { $_ -match 'python|numpy|openblas|libscipy|vcruntime' -or $_ -like '*.pyd' }
    if (-not ($python | Where-Object { $_ -match 'python3\d*\.dll' })) { throw 'The CPython library is not loaded' }
    if (-not ($python | Where-Object { $_ -like '*_multiarray_umath*' })) { throw "NumPy's extension is not loaded" }
    foreach ($module in $python) {
        if ($module -like "$system\*" -or $module -like "$env:SystemRoot\*") { continue }
        Inside $module 'A loaded module'
    }
    $outside = $modules | Where-Object { $_ -notlike "$env:SystemRoot\*" -and -not $_.StartsWith($target, [StringComparison]::OrdinalIgnoreCase) }
    $process.StandardInput.WriteLine('')
    $rest = $process.StandardOutput.ReadToEnd()
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'The application did not exit' }
    if ($process.ExitCode -ne 0) { throw "Exit code $($process.ExitCode)" }
    if ($rest -notmatch 'DONE') { throw 'The application did not finish' }
    Write-Output "Loaded Python-related modules, all inside the bundle or Windows: $($python.Count)"
    Write-Output "Modules from anywhere else on this machine: $(@($outside).Count)"
    foreach ($item in @($outside)) { Write-Output "  $item" }
    Write-Output "Python bundle isolation passed: $checks checks"
} finally {
    Pop-Location
}
