#requires -Version 7
# Qualifies a tools/release/build-windows.ps1 output. See docs/releasing.md.
# Every step runs, and results are appended, so a failed attempt stays in the record beside its rerun.
# Run in a Visual Studio x64 developer shell. Redirected profiles only; the real PATH and registry are untouched.
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [string]$Code = '',
    [string]$OpenSSLDirectory = '',
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Continue'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# CMake reads these paths too, and treats a backslash as an escape.
$Build = [IO.Path]::GetFullPath($Build).Replace('\', '/')
Set-Location $root
$env:PATH = "$env:SystemRoot\System32;$env:PATH"
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$evidence = "$Build/qualify-$stamp"
New-Item -ItemType Directory $evidence | Out-Null
$env:TMP = "$Build\tmp"; $env:TEMP = "$Build\tmp"
$results = "$Build/qualification-windows.txt"
$wood = "$Build/package/payload/wood-native/bin/wood.exe"
$data = "$Build/package/payload/wood-native/share/wood"
$setup = "$Build/release/wood-setup.exe"
$commit = (git rev-parse HEAD).Trim()
$sdk = Join-Path $env:WindowsSdkDir ('Lib/' + $env:WindowsSDKVersion.TrimEnd('\') + '/um/x64')

function Record([string]$status, [string]$name, [string]$detail) {
    $line = "$status windows $name $($detail -replace '\s+', ' ')"
    Add-Content -LiteralPath $results $line
    Write-Output "$line   [$((Get-Date).ToString('HH:mm:ss'))]"
}
# Runs a check. It passes only when the action exits zero and, if given, the log has the expected success line.
function Check([string]$name, [string]$expect, [scriptblock]$action) {
    if ($Only.Count -and $Only -notcontains $name) { return }
    $log = "$evidence/$name.log"
    $global:LASTEXITCODE = 0
    try {
        & $action *> $log
        $code = $LASTEXITCODE
    } catch {
        "EXCEPTION: $_" | Add-Content -LiteralPath $log
        $code = 1
    }
    $tail = (Get-Content -LiteralPath $log -Tail 40 -ErrorAction SilentlyContinue) -join "`n"
    $matched = if ($expect) { [regex]::Matches($tail, $expect) } else { @() }
    if ($code -eq 0 -and (-not $expect -or $matched.Count)) {
        Record 'PASS' $name $(if ($expect) { $matched[$matched.Count - 1].Value } else { 'exit 0' })
    } else {
        $last = (Get-Content -LiteralPath $log -Tail 1 -ErrorAction SilentlyContinue) -join ''
        Record 'FAIL' $name "exit $code; see $log; $($last.Substring(0, [Math]::Min(180, $last.Length)))"
    }
}

Write-Output "commit $commit, evidence $evidence"
if ($Only.Count -eq 0) { Add-Content -LiteralPath $results "RUN windows $stamp commit $commit" }

Check 'wizard' 'WIZARD-OK \d+ checks' {
    & "$root/tools/installer/qualify-windows.ps1" -Setup $setup -Evidence "$evidence/wizard" -Builder "$Build/installer-builder.exe" -Template "$Build/release-setup-template.exe" -Payload "$Build/package/payload/wood-native"
    $status = $LASTEXITCODE
    if ($status -eq 0 -and (Test-Path "$evidence/wizard/result.txt")) { "WIZARD-OK $((Get-Content "$evidence/wizard/checks.txt").Count) checks" }
    $global:LASTEXITCODE = $status
}
Check 'portable' 'PORTABLE-OK' {
    & "$root/tools/installer/qualify-portable.ps1" -Setup $setup -EngineCLI "$Build/installer-cli.exe" -ReferenceNative "$Build/stage1-reference/wood-native/bin/wood.exe" -Evidence "$evidence/portable"
    if (Test-Path "$evidence/portable/result.txt") { Get-Content "$evidence/portable/checks.txt"; "PORTABLE-OK $((Get-Content "$evidence/portable/checks.txt").Count) checks" }
}
Check 'version' '^wood 1\.1\.0 \(language 0\.4\.0, native runtime ABI 4\)' { & $wood --version }
Check 'diagnostics' 'Diagnostics quality passed: \d+ checks' { cmake "-DWOOD=$wood" "-DWORK=$evidence/diagnostics" -P tests/diagnostics_quality.cmake }
Check 'heap' '' { & $wood run tests/runtime_heap.wd }
Check 'math' 'math functions ok' { & $wood run tests/math_functions.wd }
Check 'planted-programs' 'Planted programs passed' { cmake "-DWOOD=$wood" "-DROOT=$root" "-DTEST_DIR=$evidence/planted-programs" -P tests/planted_programs.cmake 2>&1 }
Check 'explanations' 'Explanations passed' { cmake "-DWOOD=$wood" "-DROOT=$root" "-DTEST_DIR=$evidence/explanations" -P tests/explanations.cmake 2>&1 }
Check 'sqlite' 'native SQLite lifecycle passed' { & $wood run tests/native_sqlite.wd 2>&1 }
if ($OpenSSLDirectory) {
    $env:WOOD_OPENSSL_DIR = $OpenSSLDirectory
    $env:SSL_CERT_FILE = "$root/tests/fixtures/tls/ca.pem"
    $env:SSL_CERT_DIR = "$evidence/no-extra-roots"
    $env:WOOD_REQUIRE_TLS_TESTS = '1'
    Check 'tls' 'native TLS lifecycle passed' { & $wood run tests/native_tls.wd -- "$root/tests/fixtures/tls" 2>&1 }
    Check 'tls-ipv6' 'IPv6 TLS passed' { & $wood run tests/native_tls_ipv6.wd -- "$root/tests/fixtures/tls/ip.pem" "$root/tests/fixtures/tls/server-key.pem" 2>&1 }
    Remove-Item Env:WOOD_OPENSSL_DIR, Env:SSL_CERT_FILE, Env:SSL_CERT_DIR, Env:WOOD_REQUIRE_TLS_TESTS
}
New-Item -ItemType Directory "$evidence/fuzz" | Out-Null
Check 'fuzz-parsers' 'fuzz parsers ok' { & $wood run tests/fuzz/parsers.wd -- 1 2000 "$evidence/fuzz" 2>&1 }
Check 'fuzz-compiler' ' 0 crashes' { node tests/fuzz/compiler.mjs $wood 1 120 "$evidence/fuzz/compiler" tests examples tools/playground/examples stdlib 2>&1 }
Check 'soak' 'soak ok' { & $wood run tests/soak.wd -- 120 "$evidence/fuzz" 2>&1 }
Check 'package-manager' 'Package manager workflow passed: \d+ checks' { cmake "-DWOOD=$wood" "-DWORK=$evidence/pm" "-DSOURCE_DIR=$root" -P tests/package_manager.cmake }
Check 'official-fetch' 'Official package fetch passed: \d+ packages, \d+ commands' { cmake "-DWOOD=$wood" "-DWORK=$evidence/official-fetch" "-DSOURCE_DIR=$root" -P tests/official_fetch.cmake }
Check 'python' 'Python provider workflow passed: \d+ commands' { cmake "-DWOOD=$wood" "-DWORK=$evidence/python" -P tests/package_python.cmake }
# This test narrows PATH for itself, so it needs its own process.
Check 'python-bundle' '\d+ checks' { pwsh -NoProfile -File "$root/tools/test-python-bundle.ps1" -Wood $wood -Work "$evidence/python-bundle" }
Check 'lsp' 'pass \d+' {
    $env:WOOD = $wood
    node --test --test-timeout=240000 tests/lsp/protocol.test.mjs
    $status = $LASTEXITCODE
    Remove-Item Env:WOOD
    $global:LASTEXITCODE = $status
}
if ($Code) {
    Check 'extension' '\d+ extension tests passed' {
        $env:WOOD = $wood; $env:CODE = $Code
        node editors/vscode/test/run.mjs
        $status = $LASTEXITCODE
        Remove-Item Env:WOOD, Env:CODE
        $global:LASTEXITCODE = $status
    }
}
foreach ($package in 'wood_process', 'wood_audio', 'wood_image', 'wood_log', 'wood_config', 'wood_cli', 'wood_installer') {
    Check "package-$package" '[1-9]\d* passed, 0 failed' { & $wood test "packages/$package" }
}

# Real applications. Adapters are built here, as the example pages describe.
$applications = "$evidence/applications"
Check 'adapters' '' {
    New-Item -ItemType Directory -Force $applications | Out-Null
    foreach ($package in 'wood_windowsgui', 'wood_audio') {
        & "$LLVMDirectory/clang.exe" -x c -std=c17 --target=x86_64-pc-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -Werror -c "$root/packages/$package/native/windows.c" -o "$applications/$package.o"
        if ($LASTEXITCODE) { return }
        & "$LLVMDirectory/lld-link.exe" /lib "/out:$applications/$package.lib" "$applications/$package.o"
        if ($LASTEXITCODE) { return }
    }
}
$systemLibraries = @('--link-library', "$sdk/User32.Lib", '--link-library', "$sdk/Gdi32.Lib", '--link-library', "$sdk/Kernel32.Lib")
Check 'media-desk' 'PASS: \d+ checks' {
    & $wood build "$root/examples/media_desk" -o "$applications/media-desk.exe" --link-library "$applications/wood_windowsgui.lib" --link-library "$applications/wood_audio.lib" @systemLibraries
    if ($LASTEXITCODE) { return }
    & "$root/tools/test-media-desk.ps1" -Application "$applications/media-desk.exe" -TestDirectory "$evidence/media-desk"
}
Check 'notes-gui' '' {
    & $wood build "$root/examples/desktop_notes" --locked -O0 -o "$applications/wood-notes.exe" --link-library "$applications/wood_windowsgui.lib" @systemLibraries
    if ($LASTEXITCODE) { return }
    & "$root/tools/test-windowsgui-ui.ps1" -Application "$applications/wood-notes.exe" -TestDirectory "$evidence/notes"
}
Check 'image-tool' 'IMAGE-OK' {
    & $wood build "$root/examples/image_tool" -o "$applications/image_tool.exe"
    if ($LASTEXITCODE) { return }
    & "$applications/image_tool.exe" draw "$applications/card.png"
    if ($LASTEXITCODE) { return }
    & "$applications/image_tool.exe" resize "$applications/card.png" "$applications/big.png" 320 200
    if ($LASTEXITCODE) { return }
    if ((Test-Path "$applications/big.png") -and (Get-Item "$applications/big.png").Length -gt 0) { 'IMAGE-OK' }
}
Check 'audio-tool' 'AUDIO-OK' {
    & $wood build "$root/examples/audio_player" -o "$applications/player.exe" --link-library "$applications/wood_audio.lib" @systemLibraries
    if ($LASTEXITCODE) { return }
    & "$applications/player.exe" --tone "$applications/chord.wav"
    if ($LASTEXITCODE) { return }
    if ((Test-Path "$applications/chord.wav") -and (Get-Item "$applications/chord.wav").Length -gt 44) { 'AUDIO-OK' }
}

Check 'reference' 'tests passed' {
    cmake -S . -B "$Build/reference" -G Ninja -DCMAKE_BUILD_TYPE=Release -DWOOD_PYTHON_TESTS=OFF "-DWOOD_NATIVE_FOR_PACKAGE_TESTS=$wood"
    if ($LASTEXITCODE) { return }
    cmake --build "$Build/reference" --parallel 4
    if ($LASTEXITCODE) { return }
    ctest --test-dir "$Build/reference" --output-on-failure --timeout 2400
}
Check 'release-audit' '"blocking_findings": 0' { pwsh -NoProfile -File tools/release-audit.ps1 -CheckDocs }
Write-Output "WINDOWS QUALIFY DONE $results"
