#requires -Version 7
param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$EngineCLI,
    [Parameter(Mandatory)][string]$ReferenceNative,
    [Parameter(Mandatory)][string]$Evidence
)
$ErrorActionPreference='Stop'
$Evidence=[IO.Path]::GetFullPath($Evidence)
if(Test-Path -LiteralPath $Evidence){throw 'Use a fresh evidence directory'}
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
function Run([string]$Exe,[string[]]$Arguments){
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=[IO.Path]::GetFullPath($Exe)
    foreach($arg in $Arguments){$info.ArgumentList.Add($arg)}
    $info.WorkingDirectory=$Evidence;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $info.Environment['PATH']="$env:SystemRoot\System32";$info.Environment['LIB']='';$info.Environment['INCLUDE']=''
    $info.Environment['TMP']="$Evidence/tmp";$info.Environment['TEMP']="$Evidence/tmp"
    [IO.Directory]::CreateDirectory("$Evidence/tmp")|Out-Null
    $p=[Diagnostics.Process]::Start($info)
    try{
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
        if(!$p.WaitForExit(600000)){throw 'INCOMPLETE: owned native command timed out'}
        $out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
        Add-Content "$Evidence/commands.log" "$Exe $($Arguments -join ' ')`nexit=$($p.ExitCode)`n$out$err"
        if($p.ExitCode){throw "Command failed: $($Arguments -join ' ')"}
        return $out
    }finally{if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}
}
$root="$Evidence/portable"
Run $EngineCLI @('install',[IO.Path]::GetFullPath($Setup),$root)|Out-Null
$receipt=Get-Content "$root/receipt.json" -Raw|ConvertFrom-Json
$generation=Join-Path $root $receipt.active
$data=Join-Path $generation 'share/wood'
$raw=Join-Path $generation 'bin/wood-compiler.exe'
$cli=Join-Path $generation 'bin/wood.exe'
Copy-Item -LiteralPath $raw -Destination "$Evidence/stage1.exe"
Run $cli @('--version')|Out-Null
$source=Join-Path $data 'bootstrap/src/driver_main.wd'
for($stage=2;$stage -le 3;$stage++){
    $output="$Evidence/stage$stage.exe"
    Run $cli @('build',$source,'-O0','-o',$output)|Out-Null
    Copy-Item -LiteralPath $output -Destination $raw -Force
    Run $cli @('--version')|Out-Null
    Run $cli @('emit-llvm',$source,'-o',"$Evidence/stage$stage.ll")|Out-Null
}
foreach($suffix in @('exe','ll')){
    if((Get-FileHash "$Evidence/stage2.$suffix").Hash -ne (Get-FileHash "$Evidence/stage3.$suffix").Hash){throw "Bootstrap $suffix differs"}
}
# A remapped release stage one can differ from stages built with the user's own
# local filenames. Stages two/three must nevertheless be identical.
Add-Content "$Evidence/checks.txt" 'PASS: native stages two and three have identical IR and executable bytes'
$fixtures=@('examples/mesh_import.wd','tests/native_arrays.wd','tests/native_callables.wd')
foreach($relative in $fixtures){
    $input=Join-Path $data $relative
    if(!(Test-Path -LiteralPath $input)){throw "Required fixture missing: $relative"}
    $name=[IO.Path]::GetFileNameWithoutExtension($input)
    Run $cli @('emit-llvm',$input,'-o',"$Evidence/$name.candidate.ll")|Out-Null
    Run $ReferenceNative @('emit-llvm',$input,'-o',"$Evidence/$name.reference.ll")|Out-Null
    if((Get-FileHash "$Evidence/$name.candidate.ll").Hash -ne (Get-FileHash "$Evidence/$name.reference.ll").Hash){throw "Reference IR differs: $relative"}
    Add-Content "$Evidence/checks.txt" "PASS: reference/native IR matches for $relative"
}
Run $cli @('run',"$data/examples/mesh_import.wd",'--',"$data/tests/fixtures/obj/tetrahedron.obj","$Evidence/transformed.obj")|Out-Null
Run $cli @('run',"$data/examples/mesh_import.wd",'--',"$Evidence/transformed.obj","$Evidence/roundtrip.obj")|Out-Null
if(!(Test-Path "$Evidence/roundtrip.obj")){throw 'Mesh application produced no output'}
Add-Content "$Evidence/checks.txt" 'PASS: packaged OBJ import/transform/export application and reimport'
Run $cli @('init',"$Evidence/project")|Out-Null
foreach($command in @('lock','check','deps','inspect','test','build','run')){Run $cli @($command,"$Evidence/project")|Out-Null}
Add-Content "$Evidence/checks.txt" 'PASS: project workflows after native self-rebuilding'
# Relocate a separate copy; never move the user's actual installation.
$relocated="$Evidence/relocated"
Copy-Item -LiteralPath $generation -Destination $relocated -Recurse
Run "$relocated/bin/wood.exe" @('run',"$Evidence/project")|Out-Null
Add-Content "$Evidence/checks.txt" 'PASS: relocated native toolchain builds/runs with bundled tools only'
[IO.File]::WriteAllText("$Evidence/result.txt",'PASS: isolated SDK-free process profile, native bootstrap, reference IR, mesh and relocation')
