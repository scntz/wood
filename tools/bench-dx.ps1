param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$Output,
      [int]$Runs = 7)
$ErrorActionPreference = 'Stop'
# Repeatable developer-experience timings: real commands on real projects in this repository.
# Each case reports the median and spread of $Runs fresh processes, and the peak working set.
$repository = Split-Path -Parent $PSScriptRoot
$work = Join-Path ([IO.Path]::GetTempPath()) ("wood-bench-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null

function Measure-Case([string]$Name, [string[]]$Arguments, [int]$Expected = 0) {
    $times = [Collections.Generic.List[double]]::new()
    $peak = 0L
    for ($i = 0; $i -lt $Runs; $i++) {
        $start = [Diagnostics.ProcessStartInfo]::new($Wood)
        foreach ($item in $Arguments) { $start.ArgumentList.Add($item) }
        $start.UseShellExecute = $false
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.WorkingDirectory = $repository
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($start)
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        while (-not $process.WaitForExit(5)) {
            try { $process.Refresh(); if ($process.PeakWorkingSet64 -gt $peak) { $peak = $process.PeakWorkingSet64 } } catch { }
        }
        $clock.Stop()
        $process.WaitForExit()
        if ($process.ExitCode -ne $Expected) { throw "$Name exited with $($process.ExitCode): $($err.Result)" }
        $times.Add($clock.Elapsed.TotalMilliseconds)
    }
    $sorted = $times | Sort-Object
    [pscustomobject]@{
        case = $Name; runs = $Runs
        median_ms = [Math]::Round($sorted[[int]($Runs / 2)], 1)
        min_ms = [Math]::Round($sorted[0], 1); max_ms = [Math]::Round($sorted[-1], 1)
        peak_mb = [Math]::Round($peak / 1MB, 1)
    }
}

$hello = Join-Path $work 'hello.wd'
Set-Content -LiteralPath $hello -Value 'print("hello")' -NoNewline
$results = @(
    Measure-Case 'startup: --version' @('--version')
    Measure-Case 'check: one-line script' @('check', $hello)
    Measure-Case 'check: stdlib-heavy example' @('check', 'examples/text_report.wd')
    Measure-Case 'check: multi-module application' @('check', 'examples/media_desk')
    Measure-Case 'check: the compiler itself' @('check', 'bootstrap/src/driver_main.wd')
    Measure-Case 'emit-llvm: one-line script' @('emit-llvm', $hello, '-o', (Join-Path $work 'a.ll'))
    Measure-Case 'emit-llvm: multi-module application' @('emit-llvm', 'examples/media_desk', '-o', (Join-Path $work 'b.ll'))
)
Remove-Item -LiteralPath $work -Recurse -Force
$results | Format-Table | Out-String | Write-Host
$results | ConvertTo-Json | Set-Content -LiteralPath $Output
