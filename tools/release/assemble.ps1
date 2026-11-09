#requires -Version 7
# Gathers one release's files into a new directory, with checksums, the qualification manifest and release body.
# See docs/releasing.md. Publishes nothing.
param(
    [Parameter(Mandatory)][string]$WindowsBuild,
    [Parameter(Mandatory)][string]$LinuxRelease,
    [Parameter(Mandatory)][string]$LinuxBuildInfo,
    [Parameter(Mandatory)][string]$QualificationLinux,
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$WindowsBuild = [IO.Path]::GetFullPath($WindowsBuild)
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw "Output must not exist: $Output" }
$inputs = Get-Content (Join-Path $PSScriptRoot 'inputs.json') -Raw | ConvertFrom-Json
$version = $inputs.distribution
$windowsInfo = Get-Content "$WindowsBuild/build-info.json" -Raw | ConvertFrom-Json
$linuxInfo = Get-Content -LiteralPath $LinuxBuildInfo -Raw | ConvertFrom-Json
$commit = $windowsInfo.source_commit
if ($linuxInfo.source_commit -ne $commit) { throw "Platforms built different commits: $commit and $($linuxInfo.source_commit)" }
$short = $commit.Substring(0, 7)

# The source archive must be exactly this commit, whichever machine made it.
$tree = Join-Path ([IO.Path]::GetTempPath()) "wood-source-tree-$short.tar"
& git -C $root archive --format=tar -o $tree $commit
if ($LASTEXITCODE) { throw 'git archive failed' }
$treeHash = (Get-FileHash -LiteralPath $tree).Hash.ToLowerInvariant()
[IO.File]::Delete($tree)
if ($treeHash -ne $linuxInfo.source_tree_sha256) { throw "Source tree differs from Linux's: $treeHash and $($linuxInfo.source_tree_sha256)" }

New-Item -ItemType Directory $Output | Out-Null
$names = @(
    @{ name = 'wood-setup.exe'; from = "$WindowsBuild/release"; purpose = 'Windows x86-64 setup wizard' },
    @{ name = 'wood-core-windows-x86_64.tar.gz'; from = "$WindowsBuild/release"; purpose = 'Windows x86-64 portable archive' },
    @{ name = 'wood-setup-linux-x86_64'; from = $LinuxRelease; purpose = 'Linux x86-64 installer' },
    @{ name = 'wood-core-linux-x86_64.tar.gz'; from = $LinuxRelease; purpose = 'Linux x86-64 portable archive' },
    @{ name = "wood-source-$short.tar.gz"; from = $LinuxRelease; purpose = 'Source of the frozen release commit' },
    @{ name = "wood-$version.vsix"; from = "$WindowsBuild/release"; purpose = 'VS Code extension' }
)
$assets = foreach ($item in $names) {
    $source = Join-Path $item.from $item.name
    Copy-Item -LiteralPath $source -Destination (Join-Path $Output $item.name)
    $file = Get-Item -LiteralPath (Join-Path $Output $item.name)
    [ordered]@{ name = $item.name; purpose = $item.purpose; size = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() }
}

# Every recorded attempt is kept, including failures that a later rerun corrected. Log paths belong to the build machine.
function Read-Results([string]$file) {
    foreach ($line in [IO.File]::ReadAllLines($file)) {
        if ($line -match '^(PASS|FAIL) (\S+) (\S+) ?(.*)$') { [ordered]@{ status = $Matches[1]; platform = $Matches[2]; check = $Matches[3]; detail = $Matches[4] -replace '; see [^;]+;', ';' } }
    }
}
$attempts = @(Read-Results "$WindowsBuild/qualification-windows.txt") + @(Read-Results $QualificationLinux)
$latest = [ordered]@{}
foreach ($attempt in $attempts) { $latest["$($attempt.platform)/$($attempt.check)"] = $attempt }
$failing = @($latest.Values | Where-Object { $_.status -ne 'PASS' })
$manifest = [ordered]@{
    format = 1; distribution = $version; language = $inputs.language; runtime_abi = $inputs.runtime_abi
    source_commit = $commit; source_tree_sha256 = $treeHash; source_epoch = $windowsInfo.source_epoch
    tag = "v$version"; prerelease = $false
    seed = [ordered]@{ windows_sha256 = $windowsInfo.seed_sha256; windows_is_pinned_release = $windowsInfo.seed_is_pinned_release; linux_sha256 = $linuxInfo.seed_sha256; linux_is_pinned_release = $linuxInfo.seed_is_pinned_release }
    assets = @($assets)
    results = [ordered]@{ checks = $latest.Count; passing_now = ($latest.Count - $failing.Count); failing_now = @($failing | ForEach-Object { "$($_.platform)/$($_.check)" }); attempts = $attempts.Count; failed_attempts = @($attempts | Where-Object { $_.status -ne 'PASS' }).Count }
    latest = @($latest.Values)
    all_attempts = @($attempts)
    not_qualified = @(
        'A clean machine: qualification redirects the user profile and registry on a development machine',
        'wood add against the real public repository, which needs the repository to be public. A local mirror of it is tested',
        'GitHub Actions on this commit. Its workflows were checked locally, and the equivalent suites ran on these two machines',
        'TLS and HTTPS on Windows when no OpenSSL 3 is installed in System32',
        'The VS Code extension in VS Code on Linux, and other editors',
        'ThreadSanitizer on the allocator, and sanitizers on Windows',
        'Audio recording, GUI accessibility and multiple-monitor DPI'
    )
    unsigned = 'The Windows installer and executables are not code signed'
    stability = 'Version 1.1.0. docs/stability.md says what is promised, and docs/known-limitations.md what is not done'
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText("$Output/qualification.json", ($manifest | ConvertTo-Json -Depth 8) + "`n", $utf8)
[IO.File]::WriteAllText("$Output/SOURCE-COMMIT.txt", "$commit`n", $utf8)

# Release notes with links that work from a release page.
$body = [IO.File]::ReadAllText("$root/docs/releases/$version.md")
$blob = [Uri]"https://github.com/scntz/wood/blob/v$version/docs/releases/"
$body = [regex]::Replace($body, '\]\((?!https?://|#)([^)]+)\)', { param($m) "]($([Uri]::new($blob, $m.Groups[1].Value).AbsoluteUri))" })
[IO.File]::WriteAllText("$Output/RELEASE_BODY.md", $body, $utf8)

# The checksum list covers every other file.
$lines = Get-ChildItem -LiteralPath $Output -File | Sort-Object Name | ForEach-Object { "$((Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant())  $($_.Name)" }
[IO.File]::WriteAllText("$Output/SHA256SUMS", ($lines -join "`n") + "`n", $utf8)
Write-Output "Assembled $Output"
Write-Output "Checks: $($latest.Count), failing now: $($failing.Count), attempts: $($attempts.Count)"
foreach ($asset in $assets) { Write-Output ("{0,12:n0}  {1}  {2}" -f $asset.size, $asset.sha256, $asset.name) }
if ($failing.Count) { Write-Output 'NOT READY. Failing now:'; $failing | ForEach-Object { Write-Output "  $($_.platform)/$($_.check): $($_.detail)" }; exit 1 }
