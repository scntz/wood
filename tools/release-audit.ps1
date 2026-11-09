param(
    [switch]$IncludeUntracked,
    [switch]$AllObjects,
    [switch]$CheckDocs,
    [string]$Report = ''
)
$ErrorActionPreference='Stop'
$root=(& git rev-parse --show-toplevel).Trim()
if($LASTEXITCODE -ne 0){throw 'Run inside the Wood Git checkout'}
Set-Location -LiteralPath $root
$policy=Get-Content .github/release-audit.json -Raw|ConvertFrom-Json
$findings=[Collections.Generic.List[object]]::new()
$entries=[Collections.Generic.List[object]]::new()
$names=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
function Finding($path,$kind,$scope){$findings.Add(@{path=$path;kind=$kind;scope=$scope})}
function Read-Blob([string]$id,[string]$path,[string]$scope){
    $size=[long](& git cat-file -s $id)
    if($LASTEXITCODE -ne 0){throw "Cannot inspect Git blob $id"}
    if($size -gt $policy.maximum_file_bytes){Finding $path 'oversize' $scope;return ,([byte[]]::new(0))}
    $start=[Diagnostics.ProcessStartInfo]::new('git')
    $start.UseShellExecute=$false; $start.RedirectStandardOutput=$true
    foreach($arg in @('cat-file','blob',$id)){[void]$start.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::Start($start)
    $stream=[IO.MemoryStream]::new()
    $process.StandardOutput.BaseStream.CopyTo($stream); $process.WaitForExit()
    if($process.ExitCode -ne 0){throw "Cannot read Git blob $id"}
    $process.Dispose(); $bytes=$stream.ToArray(); $stream.Dispose()
    return ,$bytes
}
function Inspect([string]$path,[byte[]]$bytes,[string]$scope){
    if($bytes.Length -gt $policy.maximum_file_bytes){Finding $path 'oversize' $scope}
    # A reviewed binary is exempt from text rules only at its exact path and bytes. History can hold several reviewed versions of a path.
    $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    $binary=@($policy.reviewed_binary_assets|Where-Object {$_.path -ceq $path -and $_.sha256 -ceq $hash}).Count -gt 0
    try{$text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)}catch{if(-not $binary){Finding $path 'invalid-text-encoding' $scope};$text=[Text.Encoding]::UTF8.GetString($bytes)}
    if(-not $binary -and $text.Contains([char]0)){Finding $path 'binary-NUL' $scope}
    foreach($rule in $policy.patterns){
        if([regex]::IsMatch($text,$rule.regex)){
            if($rule.name -eq 'private-key'){
                $allowed=@($policy.public_test_keys|Where-Object {$_.path -ceq $path})
                $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
                # Git's LF normalization is deliberate. Fixtures are checked as
                # normalized text so Windows CRLF checkouts have the same policy.
                $normalized=[Text.Encoding]::UTF8.GetBytes($text.Replace("`r`n","`n"))
                $normalizedHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($normalized)).ToLowerInvariant()
                if($allowed.Count -eq 1 -and ($allowed[0].sha256 -ceq $hash -or $allowed[0].sha256 -ceq $normalizedHash)){continue}
            }
            Finding $path $rule.name $scope
        }
    }
}
foreach($line in @(& git ls-files --stage)){
    if($line -notmatch '^(\d+) ([a-f0-9]{40,64}) (\d)\t(.+)$'){throw 'Unexpected Git index entry'}
    $mode=$Matches[1];$id=$Matches[2];$stage=$Matches[3];$path=$Matches[4]
    if($stage -ne '0'){throw "Unmerged file: $path"}
    if($mode -notin @('100644','100755')){Finding $path 'nonregular-index-entry' 'index'}
    [void]$names.Add($path);$entries.Add(@{path=$path;id=$id;bytes=(Read-Blob $id $path 'index');scope='index'})
}
if($IncludeUntracked){
    foreach($path in @(& git ls-files --others --exclude-standard)){
        if($names.Add($path)){
            $file=Get-Item -LiteralPath $path
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){Finding $path 'filesystem-link' 'working-tree';continue}
            if($file.Length -gt $policy.maximum_file_bytes){Finding $path 'oversize' 'working-tree';$entries.Add(@{path=$path;id='';bytes=[byte[]]::new(0);scope='working-tree'});continue}
            $entries.Add(@{path=$path;id='';bytes=[IO.File]::ReadAllBytes($file.FullName);scope='working-tree'})
        }
    }
}
$total=0L;$largest=0L
foreach($entry in $entries){
    Inspect $entry.path $entry.bytes $entry.scope
    $total+=$entry.bytes.Length;$largest=[Math]::Max($largest,$entry.bytes.Length)
    if($entry.path -match $policy.forbidden_paths){Finding $entry.path 'forbidden-public-path' $entry.scope}
    if($entry.path.EndsWith('.obj') -and -not $entry.path.StartsWith('tests/fixtures/obj/')){Finding $entry.path 'non-fixture-object' $entry.scope}
    if($entry.path -like '*.json'){
        try{$null=[Text.Encoding]::UTF8.GetString($entry.bytes)|ConvertFrom-Json -Depth 100 -AsHashtable}catch{Finding $entry.path 'invalid-JSON' $entry.scope}
    }
}
$reachable=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$head=& git rev-parse --verify --quiet HEAD
$hasHead=$LASTEXITCODE -eq 0
if($hasHead){
    foreach($line in @(& git rev-list --objects --all)){
        $parts=$line -split ' ',2;$id=$parts[0]
        if((& git cat-file -t $id) -ne 'blob'){continue}
        [void]$reachable.Add($id)
        $path=if($parts.Count -eq 2){$parts[1]}else{$id}
        Inspect $path (Read-Blob $id $path 'reachable-history') 'reachable-history'
    }
}
$unreachable=0
if($AllObjects){
    foreach($line in @(& git cat-file --batch-all-objects '--batch-check=%(objectname) %(objecttype) %(objectsize)')){
        $parts=$line -split ' '
        if($parts[1] -eq 'blob' -and -not $reachable.Contains($parts[0])){
            $unreachable++;Inspect $parts[0] (Read-Blob $parts[0] $parts[0] 'unreachable-local-object') 'unreachable-local-object'
        }
    }
}
$links=0
if($CheckDocs){
    foreach($entry in $entries|Where-Object {$_.path -like '*.md'}){
        $text=[Text.Encoding]::UTF8.GetString($entry.bytes)
        $text=[regex]::Replace($text,'(?s)```.*?```|~~~.*?~~~','')
        $text=[regex]::Replace($text,'`[^`\r\n]*`','')
        foreach($match in [regex]::Matches($text,'\]\(([^)]+)\)')){
            $target=$match.Groups[1].Value.Split('#')[0].Trim('<','>')
            if(-not $target -or $target -match '^[a-z]+:' -or $target.StartsWith('/')){continue}
            $absolute=[IO.Path]::GetFullPath((Join-Path (Split-Path (Join-Path $root $entry.path)) $target))
            $relative=[IO.Path]::GetRelativePath($root,$absolute).Replace('\','/')
            $exists=$names.Contains($relative)
            if(-not $exists){foreach($candidate in $names){if($candidate.StartsWith($relative.TrimEnd('/')+'/',[StringComparison]::Ordinal)){$exists=$true;break}}}
            if(-not $exists){Finding $entry.path ("unpublished-local-link: "+$target) 'documentation'}
            $links++
        }
    }
}
$blocking=@($findings|Where-Object {$_.scope -ne 'unreachable-local-object'})
$result=[ordered]@{schema=1;public_files=$entries.Count;public_bytes=$total;largest_file_bytes=$largest;reachable_history_blobs=$reachable.Count;unreachable_local_blobs_examined=$unreachable;local_doc_links=$links;blocking_findings=$blocking.Count;findings=@($findings);limits='Pattern/provenance review is not proof of no secrets or vulnerabilities. Unreachable objects are retained locally and are not exported by normal main/tag pushes or git archive.'}
if($Report){$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $Report -Encoding utf8}
$result|ConvertTo-Json -Depth 8
if($blocking.Count){exit 1}
