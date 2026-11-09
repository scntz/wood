param([Parameter(Mandatory)][string]$Reference,
      [Parameter(Mandatory)][string]$Native,
      [Parameter(Mandatory)][string]$GuiArchive,
      [Parameter(Mandatory)][string]$SdkLibraries,
      [Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($TestDirectory)
if(Test-Path -LiteralPath $root){throw 'Use a fresh test directory'}
[IO.Directory]::CreateDirectory($root) | Out-Null
$source=Split-Path $PSScriptRoot -Parent
$flags=@('--link-library',$GuiArchive)
foreach($name in @('User32.Lib','Gdi32.Lib','Kernel32.Lib')){
    $file=Join-Path $SdkLibraries $name
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing SDK archive: $name"}
    $flags+=@('--link-library',$file)
}
$count=0
foreach($backend in @('reference','native')){
    $wood=if($backend -eq 'reference'){$Reference}else{$Native}
    foreach($level in 0,2){
        foreach($fixture in @('main','exit','cancel')){
            $app=Join-Path $root "$fixture-$backend-O$level.exe"
            & $wood build "$source/tests/ecosystem_gui/src/$fixture.wd" "-O$level" -o $app @flags *> "$app-build.log"
            if($LASTEXITCODE){throw "GUI build failed: $fixture $backend O$level"}
            $count++
            $launch=@{FilePath=$app;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput="$app-stdout.log";RedirectStandardError="$app-stderr.log"}
            if($fixture -eq 'cancel'){$launch.ArgumentList='"'+$app+'-handshake.txt"'}
            $child=Start-Process @launch
            try {
                if(!$child.WaitForExit(60000)){throw "Owned GUI fixture timed out: $fixture"}
                $expected=if($fixture -eq 'exit'){23}else{0}
                if($child.ExitCode -ne $expected){throw "GUI $fixture $backend exit $($child.ExitCode), expected $expected"}
                $count++
            } finally {
                if(!$child.HasExited){$child.Kill();$child.WaitForExit()}
                $child.Dispose()
            }
        }
    }
    & $wood build "$source/examples/desktop_notes" -O2 -o "$root/desktop-notes-$backend.exe" @flags *> "$root/desktop-notes-$backend-build.log"
    if($LASTEXITCODE){throw "Desktop application build failed: $backend"}
}
Set-Content "$root/validation.txt" "$count GUI build/run commands passed; two real desktop applications built."
