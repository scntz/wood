#requires -Version 7
param(
    [Parameter(Mandatory)][string]$Payload,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][long]$SourceEpoch
)
$ErrorActionPreference='Stop'
$Payload=[IO.Path]::GetFullPath($Payload)
$Output=[IO.Path]::GetFullPath($Output)
$stage=$Output+'.input'
if((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $Output)){throw 'Preserve earlier evidence; choose a new output'}
if(!(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($Output)) -PathType Container)){throw 'Output parent must exist'}
[IO.Directory]::CreateDirectory($stage)|Out-Null
# Copy exactly the already verified payload inventory, excluding the inventory
# itself: the existing reproducible packager creates/verifies its own manifest.
foreach($line in [IO.File]::ReadLines("$Payload/MANIFEST.sha256")){
    if($line -notmatch '^([0-9a-f]{64})  (.+)$'){throw 'Malformed payload inventory'}
    $sha=$Matches[1];$relative=$Matches[2]
    if([IO.Path]::IsPathRooted($relative) -or $relative.Split('/') -contains '..'){throw 'Unsafe inventory name'}
    $file=Join-Path $Payload $relative
    $item=Get-Item -LiteralPath $file
    if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked/nonregular payload file'}
    for($parent=$item.Directory;$parent.FullName -ne $Payload;$parent=$parent.Parent){
        if(!$parent -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked payload ancestor'}
    }
    if((Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -ne $sha){throw "Payload hash mismatch: $relative"}
    $target=Join-Path $stage $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
    Copy-Item -LiteralPath $file -Destination $target
}
cmake "-DPREFIX=$stage" "-DOUTPUT=$Output" "-DSOURCE_DATE_EPOCH=$SourceEpoch" -P "$PSScriptRoot/../package-native.cmake" *> ($Output+'.log')
if($LASTEXITCODE){throw 'Reproducible native archive qualification failed; retain its log'}
Write-Output "Verified native archive: $Output"
