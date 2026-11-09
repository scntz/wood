$ErrorActionPreference = 'Stop'
# Only CI's backend tests need these libraries. Core builds do not link them.
$dependencyRoot = Join-Path $env:RUNNER_TEMP 'wood-native-dependencies'
$vcpkg = Join-Path $env:VCPKG_INSTALLATION_ROOT 'vcpkg.exe'
& $vcpkg install sqlite3:x64-windows openssl:x64-windows "--x-install-root=$dependencyRoot"
if ($LASTEXITCODE -ne 0) { throw 'Native test dependency installation failed' }
$binaryRoot = Join-Path $dependencyRoot 'x64-windows/bin'
foreach ($name in @('sqlite3.dll','libcrypto-3-x64.dll','libssl-3-x64.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot $name))) {
        throw "Missing required native test dependency: $name"
    }
}
"WOOD_SQLITE_DIR=$binaryRoot" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"WOOD_OPENSSL_DIR=$binaryRoot" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
