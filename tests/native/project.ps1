param(
    [Parameter(Mandatory)][string]$Emitter,
    [Parameter(Mandatory)][string]$Production,
    [Parameter(Mandatory)][string]$Stdlib,
    [Parameter(Mandatory)][string]$SourceDirectory,
    [Parameter(Mandatory)][string]$TestDirectory
)
$ErrorActionPreference='Stop'
$run=Join-Path $TestDirectory ('run-'+[guid]::NewGuid().ToString('N').Substring(0,12))
$project=Join-Path $run 'project'; $package=Join-Path $run 'package'
foreach($directory in @("$project/src","$package/src","$run/outside")){
    New-Item -ItemType Directory -Path $directory -ErrorAction Stop | Out-Null
}
$utf8=[Text.UTF8Encoding]::new($false)
function Write-Fixture([string]$Path,[string]$Content){[IO.File]::WriteAllText($Path,$Content,$utf8)}
Write-Fixture "$project/wood.json" '{"format":1,"name":"native_project","version":"1.0.0","source":"src","entry":"main.wd","dependencies":{"native_models":{"path":"../package","version":"1.0.0"}}}'
Write-Fixture "$package/wood.json" '{"format":1,"name":"native_models","version":"1.0.0","source":"src"}'
Write-Fixture "$project/src/main.wd" "from native_models.model import identity`nassert(identity(7)==7)`nprint(`"native locked project passed`")`n"
$model="fn identity[T](value:T)->T:`n    return value`n"
Write-Fixture "$package/src/model.wd" $model
$lock=& $Production lock $project 2>&1
if($LASTEXITCODE -ne 0){throw "Production lock failed: $lock"}
Write-Fixture "$run/lock.log" ($lock -join "`n")
function Emit-Project([string]$Label,[int]$Expected){
    $output=Join-Path $run ($Label+'.ll')
    $diagnostic=& $Emitter $Stdlib (Join-Path $SourceDirectory 'runtime/native') windows-x86_64 $project $output --locked 2>&1
    $code=$LASTEXITCODE; $text=$diagnostic -join "`n"; Write-Fixture "$run/$Label.log" $text
    if($code -ne $Expected){throw "$Label exit $code expected ${Expected}: $text"}
    if($Expected -eq 0){if(-not (Test-Path -LiteralPath $output)){throw 'Missing native project IR'}}
    else {
        if(Test-Path -LiteralPath $output){throw 'Rejected project published IR'}
        $data=$text | ConvertFrom-Json
        if($data.code -cne 'D5001'){throw "Incorrect project diagnostic: $text"}
    }
}
Emit-Project 'accepted' 0
[IO.File]::AppendAllText("$package/src/model.wd","`n# changed after lock`n",$utf8)
Emit-Project 'stale' 2
Write-Fixture "$package/src/model.wd" $model
# Real junction in this unique fixture only; no recursion/copy/delete follows it.
New-Item -ItemType Junction -Path "$package/src/linked" -Target "$run/outside" -ErrorAction Stop | Out-Null
Emit-Project 'junction' 2
$rejected=Get-Content -LiteralPath "$run/junction.log" -Raw | ConvertFrom-Json
if(-not $rejected.message.Contains('filesystem links are not allowed')){throw 'Junction was not rejected by source trust policy'}
Write-Fixture "$run/validation.txt" 'Native Windows locked project accepted; modified dependency rejected; real directory junction rejected; rejected builds published no IR.'
Write-Output "Native Windows project trust passed: $run"
