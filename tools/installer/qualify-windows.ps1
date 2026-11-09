param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$Evidence,
    [Parameter(Mandatory)][string]$Builder,
    [Parameter(Mandatory)][string]$Template,
    [Parameter(Mandatory)][string]$Payload
)
$ErrorActionPreference='Stop'
$Setup=[IO.Path]::GetFullPath($Setup)
$Evidence=[IO.Path]::GetFullPath($Evidence)
if(Test-Path -LiteralPath $Evidence){throw 'Use a fresh evidence directory'}
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
# All installer registry writes, including shell-dialog preferences, are confined
# by its process-local HKCU override. Never test the production registry scope.
$space=[guid]::NewGuid().ToString('N')
$profile="Software\WoodInstallerTests\$space\Profile"
[IO.File]::WriteAllText("$Evidence/namespace.txt",$space)
Add-Type @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class WoodSetupTest {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc f,IntPtr p);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,StringBuilder s,uint f,uint t,out IntPtr r);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")] static extern IntPtr Set(IntPtr h,uint m,IntPtr w,string s,uint f,uint t,out IntPtr r);
  [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW")] public static extern IntPtr Send(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);
  public static string Text(IntPtr h){var s=new StringBuilder(262145);IntPtr r;if(SendMessageTimeoutW(h,13,new IntPtr(s.Capacity),s,2,3000,out r)==IntPtr.Zero)throw new Exception("Owned window text timed out");return s.ToString();}
  public static void Text(IntPtr h,string s){IntPtr r;if(Set(h,12,IntPtr.Zero,s,2,3000,out r)==IntPtr.Zero||r==IntPtr.Zero)throw new Exception("Owned input failed");}
  public static long Message(IntPtr h,uint m,int w){IntPtr r;if(Send(h,m,new IntPtr(w),IntPtr.Zero,2,3000,out r)==IntPtr.Zero)throw new Exception("Owned message timed out");return r.ToInt64();}
  public static IntPtr[] Windows(int pid){var list=new List<IntPtr>();EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid&&IsWindowVisible(h))list.Add(h);return true;},IntPtr.Zero);return list.ToArray();}
  public static IntPtr Child(IntPtr w,int id){IntPtr found=IntPtr.Zero;EnumChildWindows(w,(h,p)=>{if(GetDlgCtrlID(h)==id)found=h;return true;},IntPtr.Zero);return found;}
}
'@
function Assert([bool]$Condition,[string]$Description){
    if(!$Condition){throw $Description}
    Add-Content -LiteralPath "$Evidence/checks.txt" -Value "PASS: $Description"
}
function Wait-For([scriptblock]$Read,[string]$Description,[int]$Seconds=180){
    $end=[DateTime]::UtcNow.AddSeconds($Seconds)
    do{
        try{$v=& $Read;if($v){return $v}}
        catch{if($_.Exception.Message -notlike '*Owned window text timed out*'){throw}}
        Start-Sleep -Milliseconds 75
    }while([DateTime]::UtcNow -lt $end)
    throw "INCOMPLETE: timed out waiting for $Description"
}
function Snapshot([string]$Path){
    $k=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path)
    if(!$k){return 'absent'}
    try{$s=@();foreach($n in ($k.GetValueNames()|Sort-Object)){$s+=@{name=$n;kind=[string]$k.GetValueKind($n);value=$k.GetValue($n,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}};return ConvertTo-Json -InputObject $s -Compress -Depth 8}finally{$k.Dispose()}
}
$realPath=Snapshot 'Environment'
$realRegistration=Snapshot 'Software\Microsoft\Windows\CurrentVersion\Uninstall\WoodInstaller.wood'
function Settings([bool]$Write=$false){
    if($Write){return [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$profile\Environment")}
    return [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("$profile\Environment")
}
function Registration([bool]$Write=$false){
    $name="$profile\Software\Microsoft\Windows\CurrentVersion\Uninstall\WoodInstaller.wood"
    if($Write){return [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($name)}
    return [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($name)
}
$children=[Collections.Generic.List[Diagnostics.Process]]::new()
function Start-Owned([string]$Exe,[string]$Arguments,[bool]$Redirect=$false,[string]$ChildPath=''){
    $p=[Diagnostics.ProcessStartInfo]::new();$p.FileName=$Exe;$p.Arguments=$Arguments
    $p.UseShellExecute=$false;$p.WorkingDirectory=$Evidence
    $p.RedirectStandardOutput=$Redirect;$p.RedirectStandardError=$Redirect;$p.CreateNoWindow=$Redirect
    $p.EnvironmentVariables['PATH']="$env:SystemRoot\System32"
    if($ChildPath -ne ''){$p.EnvironmentVariables['PATH']=$ChildPath}
    $p.EnvironmentVariables['LIB']='';$p.EnvironmentVariables['INCLUDE']=''
    $p.EnvironmentVariables['PSModulePath']="$Evidence/nonexistent-modules"
    $p.EnvironmentVariables['LOCALAPPDATA']="$Evidence\profile\AppData\Local"
    $p.EnvironmentVariables['APPDATA']="$Evidence\profile\AppData\Roaming"
    $p.EnvironmentVariables['USERPROFILE']="$Evidence\profile"
    $p.EnvironmentVariables['TEMP']="$Evidence\tmp";$p.EnvironmentVariables['TMP']="$Evidence\tmp"
    [IO.Directory]::CreateDirectory("$Evidence/tmp")|Out-Null
    [IO.Directory]::CreateDirectory("$Evidence/profile/AppData/Local")|Out-Null
    [IO.Directory]::CreateDirectory("$Evidence/profile/AppData/Roaming")|Out-Null
    $child=[Diagnostics.Process]::Start($p);$children.Add($child);return $child
}
function Window($p){
    return Wait-For {if($p.HasExited){throw "Setup exited early: $($p.ExitCode)"};foreach($h in [WoodSetupTest]::Windows($p.Id)){if([WoodSetupTest]::GetDlgItem($h,100) -ne [IntPtr]::Zero){return $h}}} 'owned setup window'
}
function Control($w,[int]$id){$h=[WoodSetupTest]::GetDlgItem($w,$id);if($h -eq [IntPtr]::Zero){throw "Missing owned control $id"};return $h}
function Click($w,[int]$id){
    $h=Control $w $id
    Wait-For {[WoodSetupTest]::IsWindowEnabled($h)} "owned control $id enabled" 30|Out-Null
    # Deliver native control notifications without activating/focusing a window.
    # BM_CLICK can silently do nothing on an inactive dialog.
    if($id -in @(11,25)){
        $checked=[WoodSetupTest]::Message($h,240,0)
        [WoodSetupTest]::Message($h,241,$(if($checked){0}else{1}))|Out-Null
    }
    if(![WoodSetupTest]::PostMessageW($w,273,[IntPtr]::new($id),$h)){throw "Cannot click $id"}
    Start-Sleep -Milliseconds 100
}
function Heading($w,[string]$expected){Wait-For {[WoodSetupTest]::Text((Control $w 1)) -eq $expected} $expected|Out-Null}
function Ready($p,[string]$Root,[bool]$Path=$true,[bool]$Browse=$false){
    $w=Window $p;Heading $w 'Welcome to wood Setup';Click $w 100;Heading $w 'License Agreement'
    Assert (![WoodSetupTest]::IsWindowEnabled((Control $w 100))) 'license acceptance gates installation'
    $license=[WoodSetupTest]::Text((Control $w 10));Assert ($license.Contains('MIT License') -and $license.Contains('Apache License')) 'actual dual-license text displayed'
    Click $w 11;Click $w 100;Heading $w 'Choose Installation Location'
    if($Browse){
        [WoodSetupTest]::Text((Control $w 21),'C:\'+('x'*1100))
        Click $w 100
        Wait-For {[WoodSetupTest]::Text((Control $w 23)).Contains('1024 UTF-8 bytes')} 'bounded installation-root diagnostic' 30|Out-Null
        Assert ([WoodSetupTest]::Text((Control $w 1)) -eq 'Choose Installation Location') 'overlong root rejected before filesystem or settings mutation'
        $before=[WoodSetupTest]::Text((Control $w 21));Click $w 22
        $dialog=Wait-For {foreach($h in [WoodSetupTest]::Windows($p.Id)){if($h -ne $w -and [WoodSetupTest]::GetDlgItem($h,2) -ne [IntPtr]::Zero){return $h}}} 'owned native folder browser' 30
        Click $dialog 2
        Wait-For {![WoodSetupTest]::IsWindowVisible($dialog)} 'folder browser cancellation' 30|Out-Null
        Assert ([WoodSetupTest]::Text((Control $w 21)) -eq $before) 'native Browse opens and cancellation preserves location'
        Click $w 22
        $dialog=Wait-For {foreach($h in [WoodSetupTest]::Windows($p.Id)){if($h -ne $w -and [WoodSetupTest]::GetDlgItem($h,2) -ne [IntPtr]::Zero){return $h}}} 'owned native folder selection' 30
        $folder=[WoodSetupTest]::Child($dialog,1152) # standard edt1, nested on Win11
        if($folder -eq [IntPtr]::Zero){throw 'INCOMPLETE: unsupported folder-dialog control layout'}
        [WoodSetupTest]::Text($folder,$Evidence)
        Click $dialog 1
        # An absolute path first navigates; a second Select Folder accepts it.
        Start-Sleep -Milliseconds 500
        if([WoodSetupTest]::IsWindowVisible($dialog)){Click $dialog 1}
        Wait-For {![WoodSetupTest]::IsWindowVisible($dialog)} 'folder selection closes' 30|Out-Null
        Wait-For {[IO.Path]::GetFullPath([WoodSetupTest]::Text((Control $w 21))) -eq $Evidence} 'selected folder publication' 30|Out-Null
        Assert ([IO.Directory]::Exists([WoodSetupTest]::Text((Control $w 21)))) 'native Browse selects the requested existing folder'
    }
    [WoodSetupTest]::Text((Control $w 21),$Root)
    Click $w 100;Heading $w 'Additional Tasks'
    Assert ([WoodSetupTest]::Message((Control $w 25),240,0) -eq 1) 'Add to PATH defaults on'
    if(!$Path){Click $w 25}
    Assert ([WoodSetupTest]::Text((Control $w 26)).Contains('No desktop shortcut')) 'CLI installer offers no desktop shortcut'
    Click $w 100;Heading $w 'Ready to Install'
    $summary=[WoodSetupTest]::Text((Control $w 28))
    Add-Content "$Evidence/summaries.txt" $summary
    Assert ($summary.Replace('\','/').ToLowerInvariant().Contains($Root.Replace('\','/').ToLowerInvariant()) -and $summary.Contains('Add to PATH: '+$(if($Path){'Yes'}else{'No'}))) 'summary matches consent and selected location'
    return $w
}
function Finish($p,$w,[bool]$Success){
    Heading $w 'Completing wood Setup'
    $text=[WoodSetupTest]::Text((Control $w 40))
    Add-Content "$Evidence/finish-messages.txt" $text
    Assert ($text.Contains('wood --version') -eq $Success) 'finish status accurately reports result'
    Click $w 100
    Assert ($p.WaitForExit(30000)) 'setup exited after Finish'
    Assert ($p.ExitCode -eq $(if($Success){0}else{1})) 'setup exit code matches result'
}
function Run-CLI([string]$Exe,[string]$Arguments,[int]$Expected=0){
    $p=Start-Owned $Exe $Arguments $true
    $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
    Assert ($p.WaitForExit(300000)) "installed CLI completed: $Arguments"
    $out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
    Add-Content "$Evidence/cli.log" "$Arguments`nexit=$($p.ExitCode)`n$out$err"
    Assert ($p.ExitCode -eq $Expected) "installed CLI result: $Arguments"
    return $out
}
try{
    $root="$Evidence/wood installé"
    $p=Start-Owned $Setup "--sandbox $space";$w=Window $p
    Click $w 102;Assert ($p.WaitForExit(30000) -and $p.ExitCode -eq 1) 'Welcome cancellation makes no installation'
    Assert (!(Test-Path -LiteralPath "$root/receipt.json")) 'cancelled setup has no receipt'
    $k=Settings;Assert (!$k -or $k.GetValue('Path',$null) -eq $null) 'cancelled setup leaves isolated PATH absent';if($k){$k.Dispose()}
    $k=Settings $true;$k.SetValue('Path','%TEMP%\original;C:\Unrelated',[Microsoft.Win32.RegistryValueKind]::ExpandString);$k.Dispose()
    $cancelRoot="$Evidence/cancelled"
    $p=Start-Owned $Setup "--sandbox $space";$w=Ready $p $cancelRoot $false $true
    Click $w 100
    Wait-For {([WoodSetupTest]::Text((Control $w 30))).Length -gt 0} 'actual verification progress'|Out-Null
    [WoodSetupTest]::PostMessageW($w,273,[IntPtr]::new(102),(Control $w 102))|Out-Null
    Assert ($p.WaitForExit(30000) -and $p.ExitCode -eq 1) 'mid-verification cancellation exits without a commit'
    Assert (!(Test-Path -LiteralPath "$cancelRoot/receipt.json")) 'mid-verification cancellation commits no installation'
    $p=Start-Owned $Setup "--sandbox $space";$w=Ready $p $root
    Click $w 100;Finish $p $w $true
    $receipt=Get-Content -LiteralPath "$root/receipt.json" -Raw|ConvertFrom-Json
    $bin=Join-Path $root ($receipt.integration.bin.Substring($root.Length+1))
    $bin=$receipt.integration.bin
    $cli=Join-Path $bin 'wood.exe'
    Assert (Test-Path -LiteralPath $cli -PathType Leaf) 'real standalone archive deployed native compiler'
    $k=Settings;Assert ($k.GetValueKind('Path') -eq [Microsoft.Win32.RegistryValueKind]::ExpandString) 'PATH registry type preserved';$pathValue=$k.GetValue('Path',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);$k.Dispose()
    Assert ($pathValue.Contains($bin)) 'owned bin component published in isolated PATH'
    $k=Registration;$command=$k.GetValue('UninstallString');Assert ($command.Contains('wood-uninstall.exe') -and $command.Contains($space)) 'Installed Apps points to executable uninstaller';$k.Dispose()
    $distributionVersion=(Get-Content (Join-Path $PSScriptRoot 'config/wood.json') -Raw|ConvertFrom-Json).version
    Assert ((Run-CLI $cli '--version').Contains("wood $distributionVersion (language 0.4.0, native runtime ABI 4)")) 'installed version names the distribution and language without a developer environment'
    # Exercise command discovery in a genuinely new shell, using the redirected
    # registry's PATH. Never publish that PATH to the developer's real profile.
    $shellCode=@'
$ErrorActionPreference='Stop'
$expected='@EXPECTED@'
$project='@PROJECT@'
$resolved=(Get-Command wood -CommandType Application).Source
if([IO.Path]::GetFullPath($resolved) -ine [IO.Path]::GetFullPath($expected)){throw 'Fresh shell resolved another wood'}
wood --version
if($LASTEXITCODE){throw 'Fresh-shell version failed'}
wood init $project
if($LASTEXITCODE){throw 'Fresh-shell init failed'}
wood lock $project
if($LASTEXITCODE){throw 'Fresh-shell lock failed'}
$binary=[IO.Path]::Combine($project,'hello.exe')
wood build $project --locked -o $binary
if($LASTEXITCODE){throw 'Fresh-shell native build failed'}
& $binary
if($LASTEXITCODE){throw 'Fresh-shell standalone program failed'}
'@
    $shellCode=$shellCode.Replace('@EXPECTED@',$cli.Replace("'","''")).Replace('@PROJECT@',("$Evidence/fresh-shell-project").Replace("'","''"))
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($shellCode))
    $shellPath="$env:SystemRoot\System32;"+[Environment]::ExpandEnvironmentVariables($pathValue)
    $shell=Start-Owned "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" "-NoProfile -NonInteractive -EncodedCommand $encoded" $true $shellPath
    $shellOut=$shell.StandardOutput.ReadToEndAsync();$shellErr=$shell.StandardError.ReadToEndAsync()
    Assert ($shell.WaitForExit(300000)) 'fresh shell completes PATH-based native workflows'
    $newShellOut=$shellOut.GetAwaiter().GetResult();$newShellErr=$shellErr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText("$Evidence/fresh-shell.log",$newShellOut+$newShellErr)
    Assert ($shell.ExitCode -eq 0) 'fresh shell resolves installer-owned wood without a developer environment'
    Assert ($newShellOut.Contains('hello, wood')) 'fresh-shell built standalone executable runs'
    Run-CLI $cli 'init hello'|Out-Null
    foreach($verb in @('check','lock','deps','test','build','run')){Run-CLI $cli "$verb hello"|Out-Null}
    [IO.File]::WriteAllText("$Evidence/quoted source.wd",'print("standalone Unicode é and quoted source")'+"`n",[Text.UTF8Encoding]::new($false))
    Assert ((Run-CLI $cli ('run "'+$Evidence+'/quoted source.wd"')).Contains('standalone Unicode')) 'launcher preserves Unicode and quoted arguments'
    [IO.File]::WriteAllText("$Evidence/bad.wd","print(unknown_name)`n")
    Run-CLI $cli ('check "'+$Evidence+'/bad.wd" --diagnostics=json') 1|Out-Null
    # Missing owned data is repaired; an edited owned file is never overwritten.
    $getting=Join-Path (Split-Path $bin) 'GETTING_STARTED.txt'
    Remove-Item -LiteralPath $getting
    $p=Start-Owned $Setup "--sandbox $space";$w=Ready $p $root;Click $w 100;Finish $p $w $true
    Assert (Test-Path -LiteralPath $getting) 'repair restores missing owned file'
    Add-Content -LiteralPath $getting 'User edit must survive'
    $p=Start-Owned $Setup "--sandbox $space";$w=Ready $p $root;Click $w 100;Finish $p $w $false
    Assert ((Get-Content -LiteralPath $getting -Raw).Contains('User edit must survive')) 'repair refuses and preserves edited owned data'
    $config=Get-Content "$PSScriptRoot/config/wood.json" -Raw|ConvertFrom-Json
    $config.version='0.4.0-preview-upgrade-test'
    [IO.File]::WriteAllText("$Evidence/upgrade.json",($config|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $p=Start-Owned ([IO.Path]::GetFullPath($Builder)) ('"'+[IO.Path]::GetFullPath($Template)+'" "'+$Evidence+'/upgrade.json" "'+[IO.Path]::GetFullPath($Payload)+'" "'+$Evidence+'/upgrade.exe"') $true
    $out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync()
    Assert ($p.WaitForExit(180000) -and $p.ExitCode -eq 0) 'upgrade fixture built from the actual qualified payload'
    [IO.File]::WriteAllText("$Evidence/upgrade-build.log",$out.GetAwaiter().GetResult()+$err.GetAwaiter().GetResult())
    $p=Start-Owned "$Evidence/upgrade.exe" "--sandbox $space";$w=Ready $p $root;Click $w 100;Finish $p $w $true
    $upgraded=Get-Content -LiteralPath "$root/receipt.json" -Raw|ConvertFrom-Json
    Assert ($upgraded.history.Count -eq 2 -and $upgraded.active -ne $receipt.active) 'wizard upgrade activates and retains both generations'
    Assert ((Get-Content -LiteralPath $getting -Raw).Contains('User edit must survive')) 'upgrade preserves edits in previous generation'
    $oldBin=$bin;$bin=$upgraded.integration.bin
    $k=Settings;$pathValue=$k.GetValue('Path',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);$k.Dispose()
    Assert ($pathValue.Contains($bin) -and !$pathValue.Contains($oldBin)) 'wizard upgrade replaces only its owned PATH component'
    $k=Registration;$command=$k.GetValue('UninstallString');Assert ($k.GetValue('DisplayVersion') -eq $config.version) 'upgrade registration follows activated version';$k.Dispose()
    [IO.File]::WriteAllText("$root/user-created.txt",'preserve this')
    $k=Settings $true;$k.SetValue('Path',$pathValue+';C:\Later',[Microsoft.Win32.RegistryValueKind]::ExpandString);$k.Dispose()
    $k=Registration $true;$k.SetValue('UserAddedValue','preserve');$k.Dispose()
    # Execute precisely the registered image. Only owned test windows are used.
    if($command -notmatch '^"([^"]+)" (.*)$'){throw 'Unexpected uninstall command'}
    $uninstaller=$Matches[1];$unargs=$Matches[2]
    $p=Start-Owned $uninstaller $unargs
    $dialog=Wait-For {foreach($h in [WoodSetupTest]::Windows($p.Id)){if([WoodSetupTest]::GetDlgItem($h,6) -ne [IntPtr]::Zero){return $h}}} 'owned uninstall confirmation'
    Click $dialog 6
    Assert ($p.WaitForExit(30000) -and $p.ExitCode -eq 0) 'registered uninstaller hands off successfully'
    $worker=Wait-For {
        foreach($candidate in Get-Process -Name wood-uninstall -ErrorAction SilentlyContinue){
            try{
                if([IO.Path]::GetFullPath($candidate.Path) -eq [IO.Path]::GetFullPath($uninstaller) -and $candidate.Id -ne $p.Id){
                    # Hold its OS process handle before it exits. An enumerated
                    # Process can otherwise lose its exit-code query handle.
                    $null=$candidate.Handle
                    return $candidate
                }
            }catch{}
        }
    } 'owned uninstall worker'
    $children.Add($worker);$w=Window $worker
    Heading $w 'wood has been uninstalled';Click $w 100
    $ended=$worker.WaitForExit(30000)
    Add-Content "$Evidence/worker-result.txt" "pid=$($worker.Id) completed=$ended exit=$($worker.ExitCode)"
    Assert ($ended -and $worker.ExitCode -eq 0) 'uninstall worker completes'
    Wait-For {!(Test-Path -LiteralPath $uninstaller)} 'verified uninstaller self-image cleanup'|Out-Null
    Assert (!(Test-Path -LiteralPath "$root/receipt.json")) 'receipt removed after uninstall commit'
    Assert ((Get-Content -LiteralPath "$root/user-created.txt" -Raw) -eq 'preserve this') 'uninstall preserves user-created file'
    Assert ((Get-Content -LiteralPath $getting -Raw).Contains('User edit must survive')) 'uninstall preserves user-edited file'
    $k=Settings;Assert ($k.GetValue('Path',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) -eq '%TEMP%\original;C:\Unrelated;C:\Later') 'uninstall removes only owned PATH component';$k.Dispose()
    $k=Registration;Assert ($k.GetValue('UninstallString',$null) -eq $null -and $k.GetValue('UserAddedValue') -eq 'preserve') 'uninstall removes owned registration and preserves unknown values';$k.Dispose()
    # Qualify consent-off in another fresh redirected profile. Do not remove the
    # prior profile, which intentionally retains unrelated user settings.
    $space=[guid]::NewGuid().ToString('N');$profile="Software\WoodInstallerTests\$space\Profile"
    [IO.File]::WriteAllText("$Evidence/no-path-namespace.txt",$space)
    $noPath="$Evidence/no-path"
    $p=Start-Owned $Setup "--sandbox $space";$w=Ready $p $noPath $false;Click $w 100;Finish $p $w $true
    $k=Settings;Assert (!$k -or $null -eq $k.GetValue('Path',$null)) 'unchecking PATH leaves absent PATH untouched';if($k){$k.Dispose()}
    $p=Start-Owned $Setup ('--uninstall "'+$noPath+'" --sandbox '+$space)
    $dialog=Wait-For {foreach($h in [WoodSetupTest]::Windows($p.Id)){if([WoodSetupTest]::GetDlgItem($h,6) -ne [IntPtr]::Zero){return $h}}} 'owned external-setup uninstall confirmation'
    Click $dialog 6;$w=Window $p;Heading $w 'wood has been uninstalled';Click $w 100
    Assert ($p.WaitForExit(30000) -and $p.ExitCode -eq 0) 'trusted external setup uninstalls directly without self-removal helper'
    Assert (!(Test-Path -LiteralPath "$noPath/receipt.json")) 'PATH-off installation is fully unregistered'
    Assert (!(Test-Path -LiteralPath "$Evidence/profile/Desktop")) 'Wood creates no desktop shortcuts'
    Assert ((Snapshot 'Environment') -eq $realPath) 'real user environment unchanged'
    Assert ((Snapshot 'Software\Microsoft\Windows\CurrentVersion\Uninstall\WoodInstaller.wood') -eq $realRegistration) 'real Installed Apps registration unchanged'
    [IO.File]::WriteAllText("$Evidence/result.txt",'PASS: isolated standalone wizard, native installed CLI, repair, protected user data and registered uninstaller')
}finally{
    foreach($p in $children){try{if(!$p.HasExited){$p.Kill();$p.WaitForExit(30000)|Out-Null}}catch{}; $p.Dispose()}
    if((Snapshot 'Environment') -ne $realPath -or (Snapshot 'Software\Microsoft\Windows\CurrentVersion\Uninstall\WoodInstaller.wood') -ne $realRegistration){throw 'REAL USER SETTINGS CHANGED: stop and preserve evidence'}
}
