param(
    [Parameter(Mandatory)][string]$Wood,
    [Parameter(Mandatory)][string]$CoreArchive,
    [Parameter(Mandatory)][string]$CoreSHA256,
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [Parameter(Mandatory)][string]$Template,
    [Parameter(Mandatory)][string]$Builder,
    [Parameter(Mandatory)][string]$Launcher,
    [Parameter(Mandatory)][string]$ReleaseCompiler,
    [Parameter(Mandatory)][string]$Work,
    [string]$RuntimeObject=''
)
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Work=[IO.Path]::GetFullPath($Work)
if(Test-Path -LiteralPath $Work){throw 'Build workspace must not exist; preserve earlier outputs'}
if((Get-FileHash -LiteralPath $CoreArchive).Hash -ne $CoreSHA256){throw 'Core archive checksum mismatch'}
$pins=Get-Content (Join-Path $PSScriptRoot 'llvm-tools.json') -Raw|ConvertFrom-Json
foreach($item in $pins.files.PSObject.Properties){
    if((Get-FileHash -LiteralPath (Join-Path $LLVMDirectory $item.Name)).Hash -ne $item.Value){throw "LLVM tool checksum mismatch: $($item.Name)"}
}
New-Item -ItemType Directory -Path $Work|Out-Null
function Invoke-Checked([string]$label,[scriptblock]$action){
    & $action *> (Join-Path $Work ($label+'.log'))
    if($LASTEXITCODE -ne 0){throw "$label failed; retain its log"}
}
New-Item -ItemType Directory -Path "$Work/extract"|Out-Null
Push-Location "$Work/extract"
try{Invoke-Checked 'extract' {tar -xf $CoreArchive}}finally{Pop-Location}
$base=Join-Path $Work 'extract/wood-native'
if(!(Test-Path -LiteralPath "$base/bin/wood.exe" -PathType Leaf)){throw 'Archive lacks the native compiler'}
$rawHash=(Get-FileHash -LiteralPath "$base/bin/wood.exe").Hash.ToLowerInvariant()
$payload=Join-Path $Work 'payload/wood-native'
New-Item -ItemType Directory -Path "$payload/bin","$payload/share/wood"|Out-Null
# Preserve qualified ABI assets, but package current reviewed source/manuals.
# Do not retain obsolete files from the older baseline's source snapshot.
Copy-Item -LiteralPath "$base/share/wood/native" -Destination "$payload/share/wood/native" -Recurse
if($RuntimeObject){
    # This release changes the runtime, so the baseline's copy would not match the compiler.
    Copy-Item -Path "$source/runtime/native/*" -Destination "$payload/share/wood/native" -Recurse -Force
    Copy-Item -LiteralPath $RuntimeObject -Destination "$payload/share/wood/native/managed.o" -Force
}
foreach($name in @('stdlib.json','language.json','explanations.json','runtime.hpp')){
    if(Test-Path -LiteralPath "$base/share/wood/$name"){
        Copy-Item -LiteralPath "$base/share/wood/$name" -Destination "$payload/share/wood/$name"
    }
}
if($RuntimeObject){
    Copy-Item -LiteralPath "$source/docs/language.json" -Destination "$payload/share/wood/language.json" -Force
    & cmake "-DOUTPUT=$payload/share/wood/stdlib.json" -P "$source/tools/bundle-stdlib.cmake"
    if($LASTEXITCODE){throw 'Cannot bundle the standard library'}
    & cmake "-DOUTPUT=$payload/share/wood/explanations.json" -P "$source/tools/bundle-explanations.cmake"
    if($LASTEXITCODE){throw 'Cannot bundle the explanations'}
}
[string[]]$sourceNames=@(& git -C $source ls-files --cached --others --exclude-standard)
if($LASTEXITCODE){throw 'Cannot inventory reviewed repository sources'}
[Array]::Sort($sourceNames,[StringComparer]::Ordinal)
$sourceManifest=[Collections.Generic.List[string]]::new()
foreach($relative in $sourceNames){
    if([IO.Path]::IsPathRooted($relative) -or $relative.Split('/') -contains '..'){throw 'Unsafe source inventory path'}
    $input=Join-Path $source $relative
    $item=Get-Item -LiteralPath $input
    if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Nonregular source: $relative"}
    for($parent=$item.Directory;$parent.FullName -ne $source;$parent=$parent.Parent){
        if(!$parent -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked source ancestor: $relative"}
    }
    $target=Join-Path "$payload/share/wood" $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
    Copy-Item -LiteralPath $input -Destination $target
    $sourceManifest.Add((Get-FileHash -LiteralPath $input).Hash.ToLowerInvariant()+'  '+$relative)
}
[IO.File]::WriteAllText("$payload/share/wood/SOURCE_MANIFEST.sha256",([string]::Join("`n",$sourceManifest))+"`n",[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $ReleaseCompiler -Destination "$payload/bin/wood-compiler.exe"
Copy-Item -LiteralPath $Launcher -Destination "$payload/bin/wood.exe"
Copy-Item -LiteralPath $Template -Destination "$payload/bin/wood-uninstall.exe"
New-Item -ItemType Directory -Path "$payload/tools/llvm","$payload/lib","$payload/share/wood/licenses/llvm-19.1.7"|Out-Null
foreach($item in $pins.files.PSObject.Properties){Copy-Item -LiteralPath (Join-Path $LLVMDirectory $item.Name) -Destination "$payload/tools/llvm/$($item.Name)"}
Copy-Item -Path "$source/third_party/llvm-19.1.7/*" -Destination "$payload/share/wood/licenses/llvm-19.1.7"
Copy-Item -LiteralPath "$source/THIRD_PARTY_NOTICES.md" -Destination "$payload/share/wood/THIRD_PARTY_NOTICES.md" -Force
$config=[ordered]@{format=1;runtime_abi=4;target='windows-x86_64';llvm_dir='';linker='lld-link.exe';llvm_wsl=''}|ConvertTo-Json -Compress
[IO.File]::WriteAllText("$payload/share/wood/native.json",$config+"`n",[Text.UTF8Encoding]::new($false))
# Generate a small import archive from ABI declarations, not a redistributed SDK.
$symbols=[Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
$nativeIR=[IO.File]::ReadAllText("$payload/share/wood/native/windows-x86_64.ll")
foreach($match in [regex]::Matches($nativeIR,'declare dllimport [^\r\n]*?@([A-Za-z0-9_]+)\(')){[void]$symbols.Add($match.Groups[1].Value)}
$objectSymbols=& dumpbin.exe /nologo /symbols "$payload/share/wood/native/managed.o"
if($LASTEXITCODE){throw 'Cannot inspect ABI-4 runtime imports'}
foreach($line in $objectSymbols){if($line -match 'UNDEF.*External\s+\|\s+__imp_([A-Za-z0-9_]+)'){[void]$symbols.Add($Matches[1])}}
[IO.File]::WriteAllText("$Work/kernel32.def","LIBRARY KERNEL32.dll`nEXPORTS`n"+([string]::Join("`n",$symbols))+"`n",[Text.UTF8Encoding]::new($false))
Invoke-Checked 'kernel32-imports' {lib.exe /nologo /machine:x64 /Brepro "/def:$Work/kernel32.def" "/out:$Work/kernel32.lib"}
Copy-Item -LiteralPath "$Work/kernel32.lib" -Destination "$payload/lib/kernel32.lib"
Copy-Item -LiteralPath "$Work/kernel32.def" -Destination "$payload/lib/kernel32.def"
$revision=(& git -C $source rev-parse HEAD).Trim()
if($LASTEXITCODE){throw 'Cannot identify source revision'}
$dirty=@(& git -C $source status --porcelain).Count -gt 0
$distributionVersion=(Get-Content "$PSScriptRoot/config/wood.json" -Raw|ConvertFrom-Json).version
$info=[ordered]@{format=1;profile='wood-core-local-preview';distribution_version=$distributionVersion;target='windows-x86_64';runtime_abi=4;language_version='0.4.0';source_commit=$revision;source_has_uncommitted_changes=$dirty;core_archive_sha256=$CoreSHA256;preserved_bootstrap_compiler_sha256=$rawHash;candidate_compiler_sha256=(Get-FileHash $ReleaseCompiler).Hash.ToLowerInvariant();source_inventory_sha256=(Get-FileHash "$payload/share/wood/SOURCE_MANIFEST.sha256").Hash.ToLowerInvariant();llvm=$pins;installer='native Wood wizard';windows_cleanup='Windows PowerShell 5.1 OS component';optional_services=@('OpenSSL 3 for TLS/HTTPS','SQLite 3.38+ for databases');limits='Windows x86-64; no universal clean-host or production-readiness claim'}
[IO.File]::WriteAllText("$payload/distribution.json",($info|ConvertTo-Json -Depth 12)+"`n",[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$payload/GETTING_STARTED.txt",@'
wood Core native preview

Open a NEW terminal after setup and run: wood --version
Create a project: wood init hello
Check and run: wood check hello
               wood run hello

The native compiler, LLVM 19.1.7 tools and Core import archive are included.
Normal native builds need no Python, C++ compiler or Windows developer shell.
The separate C++ reference source build needs its own C++ toolchain; this native
bundle does not include that profile's runtime.hpp sidecar.
TLS/HTTPS needs a trusted OpenSSL 3 installation; SQLite needs SQLite 3.38+.
Experimental Python and GUI integrations remain optional.
For VS Code, install wood-1.1.0.vsix from the release page.

Uninstall using Windows Installed Apps. User additions/edits are preserved.
The Windows PowerShell OS component performs final uninstaller-image cleanup.
This is a locally qualified preview, not unrestricted production readiness.
'@,[Text.UTF8Encoding]::new($false))
$manifest=[Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $payload -Recurse -File|Sort-Object FullName|ForEach-Object{
    $relative=[IO.Path]::GetRelativePath($payload,$_.FullName).Replace('\','/')
    if($_.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked payload file: $relative"}
    if($relative -ne 'MANIFEST.sha256'){$manifest.Add((Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+$relative)}
}
[IO.File]::WriteAllText("$payload/MANIFEST.sha256",([string]::Join("`n",$manifest))+"`n",[Text.UTF8Encoding]::new($false))
Invoke-Checked 'setup-build' {& $Builder $Template "$PSScriptRoot/config/wood.json" $payload "$Work/wood-setup.exe"}
Invoke-Checked 'services-source-check' {& $Wood check "$PSScriptRoot/qualification/test_windows_services.wd"}
$sum=(Get-FileHash -LiteralPath "$Work/wood-setup.exe").Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$Work/SHA256SUMS","$sum  wood-setup.exe`n",[Text.UTF8Encoding]::new($false))
Write-Output "Prepared native setup: $Work/wood-setup.exe"
