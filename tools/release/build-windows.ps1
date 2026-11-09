#requires -Version 7
# Builds the Windows release files from a clean checkout. See docs/releasing.md.
# Run in a Visual Studio x64 developer shell. Never touches an installed wood, PATH or the registry.
param(
    [Parameter(Mandatory)][string]$SeedArchive,
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [Parameter(Mandatory)][string]$Out,
    [string]$SeedSHA256 = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Out = [IO.Path]::GetFullPath($Out)
$SeedArchive = [IO.Path]::GetFullPath($SeedArchive)
$LLVMDirectory = [IO.Path]::GetFullPath($LLVMDirectory)
Set-Location $root
if (Test-Path -LiteralPath $Out) { throw "Output must not exist: $Out" }
if ($Out.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Keep the output outside the checkout' }
foreach ($tool in 'cmake', 'ninja', 'git', 'mt', 'lib', 'dumpbin', 'node', 'npm') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool: $tool. Use a Visual Studio x64 developer shell, and see docs/releasing.md" }
}
if (-not $env:WindowsSdkDir) { throw 'The Windows SDK is not configured. Use a Visual Studio x64 developer shell' }
if (@(git status --porcelain).Count -ne 0) { throw 'The checkout must be clean' }

# Windows' own tar must come first: other tar programs on PATH read drive-letter paths differently.
$env:PATH = "$env:SystemRoot\System32;$env:PATH"
$inputs = Get-Content (Join-Path $PSScriptRoot 'inputs.json') -Raw | ConvertFrom-Json
$expectedSeed = if ($SeedSHA256) { $SeedSHA256 } else { $inputs.seed_windows_sha256 }
$seedHash = (Get-FileHash -LiteralPath $SeedArchive).Hash.ToLowerInvariant()
if ($seedHash -ne $expectedSeed.ToLowerInvariant()) { throw "Seed archive hash $seedHash is not the expected $expectedSeed" }

$commit = (git rev-parse HEAD).Trim()
$epoch = [long](git log -1 --format=%ct).Trim()
New-Item -ItemType Directory $Out, "$Out/tmp", "$Out/release", "$Out/repro", "$Out/seed", "$Out/stage1-reference" | Out-Null
$env:TMP = "$Out\tmp"; $env:TEMP = "$Out\tmp"
function Step([string]$name, [scriptblock]$action) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    & $action *> "$Out/$name.log"
    if ($LASTEXITCODE) { throw "$name failed: see $Out/$name.log" }
    Write-Output ("PASS {0} ({1:n0}s)" -f $name, $clock.Elapsed.TotalSeconds)
}
Write-Output "commit $commit epoch $epoch seed $seedHash"

# The seed is extracted twice: one copy becomes the build compiler, and the other holds stage one, for IR comparison.
Step 'seed' {
    tar -xf $SeedArchive -C "$Out/seed"
    if ($LASTEXITCODE) { throw 'seed extract' }
    tar -xf $SeedArchive -C "$Out/stage1-reference"
    # The comparison is of what the compiler emits, and the runtime text is an input to that.
    Copy-Item -Path 'runtime/native/*.ll' -Destination "$Out/stage1-reference/wood-native/share/wood/native" -Force
}
$seed = "$Out/seed/wood-native"
$wood = "$seed/bin/wood.exe"
$assets = "$seed/share/wood"
if (-not (Test-Path -LiteralPath $wood)) { throw 'The seed archive has no bin/wood.exe' }

# The standard library comes from this checkout, not the seed.
cmake "-DOUTPUT=$assets/stdlib.json" -P tools/bundle-stdlib.cmake
if ($LASTEXITCODE) { throw 'bundle stdlib' }
cmake "-DOUTPUT=$assets/explanations.json" -P tools/bundle-explanations.cmake
if ($LASTEXITCODE) { throw 'bundle explanations' }

$tools = @{ clang = "$LLVMDirectory/clang.exe"; opt = "$LLVMDirectory/opt.exe"; llc = "$LLVMDirectory/llc.exe"; lld = "$LLVMDirectory/lld-link.exe" }
foreach ($tool in $tools.Values) { if (-not (Test-Path -LiteralPath $tool)) { throw "Missing LLVM tool: $tool" } }

Step 'runtime' { cmake "-DCLANG=$($tools.clang)" -DTARGET=windows-x86_64 "-DOUTPUT=$Out/managed.o" -P tools/prepare-native-runtime.cmake }

# Two compiler generations reach the new runtime; generations two and three must then be identical.
Copy-Item -Path 'runtime/native/*' -Destination "$assets/native" -Recurse -Force
Copy-Item -LiteralPath "$Out/managed.o" -Destination "$assets/native/managed.o" -Force
$sdk = Join-Path $env:WindowsSdkDir ('Lib/' + $env:WindowsSDKVersion.TrimEnd('\') + '/um/x64')
# The seed's import archive predates this runtime's imports. The SDK's is a superset, and is used only to build.
Copy-Item -LiteralPath "$sdk/kernel32.lib" -Destination "$seed/lib/kernel32.lib" -Force
$driver = 'bootstrap/src/driver_main.wd'
Step 'stage1' { & $wood build $driver -O0 -o "$Out/stage1.exe" }
Copy-Item -LiteralPath "$Out/stage1.exe" -Destination "$seed/bin/wood-compiler.exe" -Force
# The IR reference is this source built by the seed, a different route to the same compiler. It cannot be the
# seed itself: a change to what the compiler emits would then always fail.
Copy-Item -LiteralPath "$Out/stage1.exe" -Destination "$Out/stage1-reference/wood-native/bin/wood-compiler.exe" -Force
Copy-Item -LiteralPath "$assets/stdlib.json", "$assets/explanations.json" -Destination "$Out/stage1-reference/wood-native/share/wood" -Force
Step 'stage2' {
    & $wood emit-llvm $driver -o "$Out/stage2.ll"
    if ($LASTEXITCODE) { throw 'stage2 ir' }
    & $wood build $driver -O0 -o "$Out/stage2.exe"
}
Copy-Item -LiteralPath "$Out/stage2.exe" -Destination "$seed/bin/wood-compiler.exe" -Force
Step 'stage3' {
    & $wood emit-llvm $driver -o "$Out/stage3.ll"
    if ($LASTEXITCODE) { throw 'stage3 ir' }
    & $wood build $driver -O0 -o "$Out/stage3.exe"
}
foreach ($extension in 'll', 'exe') {
    if ((Get-FileHash "$Out/stage2.$extension").Hash -ne (Get-FileHash "$Out/stage3.$extension").Hash) { throw "Bootstrap stages differ: $extension" }
}
Write-Output 'PASS bootstrap stage two and three identical (IR and executable)'
Copy-Item -LiteralPath "$Out/stage3.exe" -Destination "$seed/bin/wood-compiler.exe" -Force

# Native adapters and the release tools, all from this checkout.
Step 'adapters-configure' { cmake -G Ninja -S tools/installer/native -B "$Out/native" "-DCMAKE_C_COMPILER=$($tools.clang)" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF }
Step 'adapters' { cmake --build "$Out/native" --parallel 2 }
$engine = "$Out/native/engine/wood_installer_native.lib"
Step 'emitter' { & $wood build bootstrap/src/release_native_main.wd -O0 -o "$Out/release-emitter.exe" }
$common = @("-DEMITTER=$Out/release-emitter.exe", "-DASSETS=$assets", "-DLLVM_DIRECTORY=$LLVMDirectory", "-DSOURCE_ROOT=$root")
$windowLibraries = @($engine, "$Out/native/wood_setup_windows.lib")
foreach ($name in 'user32', 'gdi32', 'advapi32', 'comctl32', 'ole32', 'shell32', 'uuid') { $windowLibraries += "$sdk/$name.lib" }
Step 'compiler' { cmake @common '-DOPTIMIZATION=0' "-DINPUT=$driver" "-DOUTPUT=$Out/release-compiler.exe" -P tools/installer/build-release-native.cmake }
Step 'template' {
    cmake @common '-DOPTIMIZATION=2' '-DSUBSYSTEM=windows' "-DLIBRARIES=$($windowLibraries -join ';')" '-DINPUT=tools/installer/windows.wd' "-DOUTPUT=$Out/release-setup-template.exe" -P tools/installer/build-release-native.cmake
    if ($LASTEXITCODE) { throw 'template' }
    mt.exe -nologo -manifest tools/installer/setup.manifest "-outputresource:$Out/release-setup-template.exe;#1"
}
Step 'launcher' { cmake @common '-DOPTIMIZATION=2' "-DLIBRARIES=$Out/native/wood_setup_launcher.lib" '-DINPUT=tools/installer/launcher.wd' "-DOUTPUT=$Out/release-launcher.exe" -P tools/installer/build-release-native.cmake }
Step 'engine-cli' { cmake @common '-DOPTIMIZATION=2' "-DLIBRARIES=$engine" '-DINPUT=tools/installer/cli.wd' "-DOUTPUT=$Out/installer-cli.exe" -P tools/installer/build-release-native.cmake }
Step 'builder' { & $wood build tools/installer/builder.wd -O2 --link-library $engine -o "$Out/installer-builder.exe" }

# The payload, the two distribution files, and a second build of each to prove the bytes repeat.
& "$root/tools/installer/build-windows.ps1" -Wood $wood -CoreArchive $SeedArchive -CoreSHA256 $seedHash -LLVMDirectory $LLVMDirectory `
    -Template "$Out/release-setup-template.exe" -Builder "$Out/installer-builder.exe" -Launcher "$Out/release-launcher.exe" `
    -ReleaseCompiler "$Out/release-compiler.exe" -Work "$Out/package" -RuntimeObject "$Out/managed.o" *> "$Out/package.log"
if ($LASTEXITCODE) { throw "package failed: see $Out/package.log" }
Write-Output 'PASS package'
$payload = "$Out/package/payload/wood-native"
Step 'archive' { & "$root/tools/installer/archive-windows.ps1" -Payload $payload -Output "$Out/release/wood-core-windows-x86_64.tar.gz" -SourceEpoch $epoch }
Copy-Item -LiteralPath "$Out/package/wood-setup.exe" -Destination "$Out/release/wood-setup.exe"
Step 'archive-repeat' { & "$root/tools/installer/archive-windows.ps1" -Payload $payload -Output "$Out/repro/wood-core-windows-x86_64.tar.gz" -SourceEpoch $epoch }
Step 'setup-repeat' { & "$Out/installer-builder.exe" "$Out/release-setup-template.exe" "$root/tools/installer/config/wood.json" $payload "$Out/repro/wood-setup.exe" }
foreach ($name in 'wood-core-windows-x86_64.tar.gz', 'wood-setup.exe') {
    if ((Get-FileHash "$Out/repro/$name").Hash -ne (Get-FileHash "$Out/release/$name").Hash) { throw "Not reproducible: $name" }
}
Write-Output 'PASS archive and setup byte-identical across two builds'
foreach ($folder in Get-ChildItem "$Out/repro" -Directory) { [IO.Directory]::Delete($folder.FullName, $true) }

# The VS Code extension. Its packaging tool comes from npm at a pinned version.
Step 'vsix' { node editors/vscode/scripts/package.mjs "$Out/release" }
git status --porcelain | Out-File "$Out/tree-after-build.txt"

[ordered]@{
    format = 1; distribution = $inputs.distribution; language = $inputs.language; runtime_abi = $inputs.runtime_abi
    target = 'windows-x86_64'; source_commit = $commit; source_epoch = $epoch; source_tree_sha256 = ''
    seed_sha256 = $seedHash; seed_is_pinned_release = ($seedHash -eq $inputs.seed_windows_sha256.ToLowerInvariant())
    llvm_version = (& $tools.clang --version | Select-Object -First 1)
} | ConvertTo-Json | Set-Content "$Out/build-info.json" -Encoding utf8
Write-Output "WINDOWS BUILD DONE $Out"
