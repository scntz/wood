# Fresh-runner LLVM-native qualification. No protected binary is replaced.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Get-Location).Path
$work = Join-Path $root 'out/native-windows'
New-Item -ItemType Directory -Path $work | Out-Null
. "$root/.github/setup-llvm19.ps1"
$llvm = Install-WoodCI_LLVM19 (Join-Path $work 'llvm19')
& "$llvm/clang.exe" --version
& "$llvm/lld-link.exe" --version
$env:WOOD_REQUIRE_TLS_TESTS = '1'
$env:WOOD_OPENSSL_DIR = Get-WoodCI_OpenSSLDirectory
$dependencies = Join-Path $work 'dependencies'
& "$env:VCPKG_INSTALLATION_ROOT/vcpkg.exe" install sqlite3:x64-windows "--x-install-root=$dependencies"
$env:WOOD_SQLITE_DIR = Join-Path $dependencies 'x64-windows/bin'
"$((Get-FileHash "$env:WOOD_SQLITE_DIR/sqlite3.dll" -Algorithm SHA256).Hash.ToLowerInvariant())  sqlite3.dll" | Set-Content "$work/sqlite3.sha256" -Encoding utf8NoBOM
cmake -S . -B "$work/reference" -G Ninja -DCMAKE_BUILD_TYPE=Release -DWOOD_PYTHON_TESTS=OFF
cmake --build "$work/reference" --parallel 2 --target wood wood_backend_tests wood_network_app_tests wood_process_tests wood_output_fixture
$reference = "$work/reference/wood.exe"
& $reference build bootstrap/src/native_main.wd -O0 --timeout-ms 1800000 -o "$work/hosted-seed.exe"
cmake "-DREFERENCE=$reference" "-DOUTPUT=$work/assets" -P tools/prepare-native-assets.cmake
cmake "-DCLANG=$llvm/clang.exe" -DTARGET=windows-x86_64 "-DOUTPUT=$work/managed.o" -P tools/prepare-native-runtime.cmake
cmake "-DCLANG=$llvm/clang.exe" -DTARGET=windows-x86_64 "-DOUTPUT=$work/legacy.o" -P out/abi4-baseline/tools/prepare-native-runtime.cmake
cmake "-DEMITTER=$work/hosted-seed.exe" "-DASSETS=$work/assets" "-DRUNTIME_OBJECT=$work/managed.o" -DTARGET=windows-x86_64 "-DLLVM_DIR=$llvm" "-DLINKER=$llvm/lld-link.exe" "-DPREFIX=$work/install" -P tools/install-native.cmake
$wood = "$work/install/bin/wood.exe"
& $wood build bootstrap/src/native_main.wd -O0 --timeout-ms 1800000 -o "$work/emitter.exe"
foreach ($fixture in @('task','ffi')) {
    & "$llvm/clang.exe" -std=c17 -O2 -ffreestanding -fno-builtin -fno-stack-protector -c "tests/native/${fixture}_fixture.c" -o "$work/$fixture.obj"
    lib.exe /nologo "/out:$work/$fixture.lib" "$work/$fixture.obj"
}
cmake -S tests/native -B "$work/tests" -G Ninja "-DINSTALLED_CLI=$wood" "-DEMITTER=$work/emitter.exe" "-DSTDLIB=$work/assets/stdlib.json" "-DPRODUCTION=$reference" "-DRUNTIME_OBJECT=$work/managed.o" "-DLEGACY_RUNTIME_OBJECT=$work/legacy.o" "-DCLANG=$llvm/clang.exe" "-DLLC=$llvm/llc.exe" "-DOPT=$llvm/opt.exe" "-DLINKER=$llvm/lld-link.exe" "-DTASK_LIBRARY=$work/task.lib" "-DNETWORK_TEST=$work/reference/wood_network_app_tests.exe" "-DBACKEND_TEST=$work/reference/wood_backend_tests.exe" "-DOUTPUT_FIXTURE=$work/reference/wood_output_fixture.exe" "-DPWSH=$((Get-Command pwsh).Source)"
ctest --test-dir "$work/tests" --parallel 1 --output-on-failure --output-junit "$work/native-results.xml"
$common = @{Emitter="$work/emitter.exe";Stdlib="$work/assets/stdlib.json";LLVMDirectory=$llvm;Linker="$llvm/lld-link.exe";SourceDirectory=$root;RuntimeObject="$work/managed.o";Production=$reference;Cxx=(Get-Command cl.exe).Source}
foreach ($gate in @('broad','callables','arrays','ffi','math')) {
    $options = @{TestDirectory="$work/pe-$gate"}
    if ($gate -eq 'callables') { $options.CallablesOnly=$true }
    if ($gate -eq 'arrays') { $options.ArraysOnly=$true }
    if ($gate -eq 'ffi') { $options.FfiOnly=$true; $options.NativeLibrary="$work/ffi.lib" }
    if ($gate -eq 'math') { $options.MathOnly=$true }
    & tests/native/windows.ps1 @common @options
}
$epoch = git -c "safe.directory=$root" show -s --format=%ct HEAD
cmake "-DPREFIX=$work/install" "-DOUTPUT=$work/wood-native-windows-x86_64.tar.gz" "-DSOURCE_DATE_EPOCH=$epoch" -DPORTABLE_NATIVE_TOOLS=ON -P tools/package-native.cmake
