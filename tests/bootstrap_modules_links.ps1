param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
foreach ($type in @('inside', 'outside')) {
    $file = Join-Path $FixtureRoot "junction-$type.txt"
    $directory = [IO.File]::ReadAllText($file, [Text.Encoding]::UTF8)
    $link = Join-Path $directory 'src/folder'
    $target = if ($type -eq 'inside') { Join-Path $directory 'src/real' } else { Join-Path $directory 'outside' }
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
}
