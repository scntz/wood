param([Parameter(Mandatory)][string]$Report,
      [Parameter(Mandatory)][string]$Output,
      [string[]]$Workloads,
      [ValidateRange(-1,62)][int]$Cpu = -1,
      [ValidateRange(3,31)][int]$Samples = 9)
$ErrorActionPreference = 'Stop'
$reportPath = (Resolve-Path -LiteralPath $Report).Path
$original = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$affinityProcess = $null
$restoreAffinity = $false
try {
if (!$PSBoundParameters.ContainsKey('Cpu') -and $null -ne $original.cpu_logical_processor) { $Cpu = $original.cpu_logical_processor }
if ($Cpu -ge 0) {
    if (!$IsWindows) { throw '-Cpu currently supports Windows only' }
    $affinityProcess = [Diagnostics.Process]::GetCurrentProcess()
    $originalAffinity = $affinityProcess.ProcessorAffinity
    $restoreAffinity = $true
    $affinityProcess.ProcessorAffinity = [IntPtr]([long]1 -shl $Cpu)
}
if (!$Workloads) { $Workloads = @($original.sizes.psobject.Properties.Name) }
$raw = [Collections.Generic.List[object]]::new()
$kinds = if ($original.implementations) { @($original.implementations) } else { @('before','wood','cpp') }
foreach ($kind in $kinds) {
    $actual = (Get-FileHash -LiteralPath $original.artifacts."${kind}_executable" -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $original.sha256."${kind}_executable") { throw "Changed benchmark executable: $kind" }
}
foreach ($mode in $Workloads) {
    $count = $original.sizes.$mode
    $expected = @($original.raw_samples | Where-Object { $_.workload -eq $mode })[0].measurement.checksum
    if (!$count -or $null -eq $expected) { throw "Unknown workload: $mode" }
    for ($index=-2; $index -lt $Samples; $index++) {
        $order = @(for ($position=0; $position -lt $kinds.Count; $position++) { $kinds[($index+2+$position)%$kinds.Count] })
        if ($index%2) { [array]::Reverse($order) }
        foreach ($kind in $order) {
            $text = & $original.artifacts."${kind}_executable" $mode $count $original.rounds | Out-String
            if ($LASTEXITCODE -ne 0) { throw "Execution failed: $kind/$mode" }
            $data = $text | ConvertFrom-Json
            if ($Cpu -ge 0 -and $data.cpu_mask -ne ([long]1 -shl $Cpu)) { throw 'Child affinity mismatch' }
            if ($data.checksum -ne $expected -or $data.elapsed_ns -le 0) { throw "Incorrect replay: $kind/$mode" }
            if ($index -ge 0) { $raw.Add([ordered]@{workload=$mode; implementation=$kind; sample=$index; count=$count; measurement=$data}) }
        }
    }
}
$summary = [ordered]@{}
foreach ($mode in $Workloads) {
    $summary[$mode] = [ordered]@{}
    foreach ($kind in $kinds) {
        $sorted = @($raw | Where-Object { $_.workload -eq $mode -and $_.implementation -eq $kind } | ForEach-Object { [long]$_.measurement.elapsed_ns } | Sort-Object)
        $middle = [int][Math]::Floor($sorted.Count/2)
        $median = $sorted[$middle]
        if ($sorted.Count%2 -eq 0) { $median = ($sorted[$middle-1]+$median)/2.0 }
        $summary[$mode][$kind] = [ordered]@{median_ns=$median; minimum_ns=$sorted[0]; maximum_ns=$sorted[-1]}
    }
}
[ordered]@{
    schema_version=1; date_utc=[DateTime]::UtcNow.ToString('o'); source_report=$reportPath
    source_report_sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant()
    runner_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    samples=$Samples; warmups=2; rounds=$original.rounds; cpu_logical_processor=$Cpu; artifacts=$original.artifacts; sha256=$original.sha256
    methodology='Replay verified, hash-checked saved executables without rebuilding; original sizes/checksums. Rotated/reversed process order, two warmups. Same instrumentation and limits as source report.'
    summary=$summary; raw_samples=$raw
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding utf8NoBOM
$summary | ConvertTo-Json -Depth 5
} finally {
    if ($restoreAffinity) { $affinityProcess.ProcessorAffinity = $originalAffinity }
    if ($null -ne $affinityProcess) { $affinityProcess.Dispose() }
}
