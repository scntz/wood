$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Get-Location).Path
. "$root/.github/setup-llvm19.ps1"
$work = Join-Path $env:RUNNER_TEMP 'wood-native-consumer'
New-Item -ItemType Directory -Path $work | Out-Null
$llvm = Install-WoodCI_LLVM19 (Join-Path $work 'llvm19')
$archive = @(Get-ChildItem out/download -Recurse -Filter wood-native-windows-x86_64.tar.gz)
if ($archive.Count -ne 1) { throw 'Expected exactly one native candidate package' }
$expected = (Get-Content "$($archive[0].FullName).sha256").Split(' ')[0]
if ((Get-FileHash $archive[0].FullName -Algorithm SHA256).Hash -ne $expected) { throw 'Package checksum mismatch' }
tar -xf $archive[0].FullName -C $work
$sqlite = @(Get-ChildItem out/download -Recurse -Filter sqlite3.dll)
$sqliteHash = @(Get-ChildItem out/download -Recurse -Filter sqlite3.sha256)
if ($sqlite.Count -ne 1 -or $sqliteHash.Count -ne 1) { throw 'Missing independently transferred test SQLite dependency' }
$expected = (Get-Content $sqliteHash[0].FullName).Split(' ')[0]
if ((Get-FileHash $sqlite[0].FullName -Algorithm SHA256).Hash -ne $expected) { throw 'SQLite test dependency checksum mismatch' }
$env:WOOD_SQLITE_DIR = $sqlite[0].DirectoryName
$env:WOOD_OPENSSL_DIR = Get-WoodCI_OpenSSLDirectory
$env:WOOD_REQUIRE_TLS_TESTS = '1'
$cmake = (Get-Command cmake).Source
# Use explicit LLVM paths and the SDK LIB inherited from the developer shell.
# No compiler, Git, Python or CMake directory is needed on the application's PATH.
$env:PATH = "$env:SystemRoot/system32;$env:SystemRoot"
$env:CXX = 'deliberately-missing'
foreach ($name in @('cl.exe','g++.exe','clang++.exe')) {
    if (Get-Command $name -ErrorAction SilentlyContinue) { throw "C++ compiler is visible: $name" }
}
$prefix = Join-Path $work 'wood-native'
& $cmake "-DPREFIX=$prefix" "-DTEST_DIR=$work/application-data" "-DLLVM_DIR=$llvm" "-DLINKER=$llvm/lld-link.exe" -P "$prefix/share/wood/tools/qualify-native-consumer.cmake"
Copy-Item "$work/application-data/consumer-validation.txt" "$root/out/consumer-validation.txt"
