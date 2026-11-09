function Install-WoodCI_LLVM19([string]$Directory) {
    $ErrorActionPreference = 'Stop'
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $archive = "$Directory.tar.xz"
    # Official archive, independently signature-verified during local native
    # qualification. Pin bytes; do not trust a mutable package-manager version.
    Invoke-WebRequest 'https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/clang%2Bllvm-19.1.7-x86_64-pc-windows-msvc.tar.xz' -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne 'b4557b4f012161f56a2f5d9e877ab9635cafd7a08f7affe14829bd60c9d357f0') {
        throw 'Official LLVM archive checksum mismatch'
    }
    tar -xf $archive -C $Directory --strip-components 1
    if ($LASTEXITCODE) { throw 'LLVM archive extraction failed' }
    return (Join-Path $Directory 'bin')
}

function Get-WoodCI_OpenSSLDirectory([string]$GitRoot = (Split-Path (Split-Path (Get-Command git).Source))) {
    # Git for Windows ships either the current UCRT or the older MinGW layout.
    # Require the complete pair from one installation; never mix DLL directories.
    foreach ($layout in @('ucrt64/bin', 'mingw64/bin')) {
        $directory = Join-Path $GitRoot $layout
        if ((Test-Path -LiteralPath (Join-Path $directory 'libssl-3-x64.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $directory 'libcrypto-3-x64.dll') -PathType Leaf)) {
            return $directory
        }
    }
    throw 'Git for Windows must provide matching OpenSSL 3 DLLs in ucrt64/bin or mingw64/bin'
}
