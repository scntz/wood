# Developer-only fixture regeneration on PowerShell 7/.NET. Keys are public test
# material, never deployment identities. Tests consume PEM files, not this script.
param([switch]$NativeVariantsOnly)
$ErrorActionPreference = 'Stop'
$fixtureRoot = Join-Path $PSScriptRoot 'fixtures/tls'
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
function Save-Fixture([string]$name, [string]$value) {
    [IO.File]::WriteAllText((Join-Path $fixtureRoot $name), $value + "`n", [Text.UTF8Encoding]::new($false))
}
function Save-NativeVariants($key) {
    foreach ($variant in @('ip', 'cn-only')) {
        $subject = if ($variant -eq 'ip') { 'CN=unused.invalid' } else { 'CN=localhost' }
        $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
            $subject, $key, [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.RSASignaturePadding]::Pkcs1)
        $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
        $usage = [Security.Cryptography.OidCollection]::new()
        $usage.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
        $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usage,$true))
        if ($variant -eq 'ip') {
            $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
            $san.AddIpAddress([Net.IPAddress]::Loopback)
            $san.AddIpAddress([Net.IPAddress]::IPv6Loopback)
            $request.CertificateExtensions.Add($san.Build())
        }
        $certificate = $request.CreateSelfSigned([DateTimeOffset]::new(2024,1,1,0,0,0,[TimeSpan]::Zero), [DateTimeOffset]::new(2040,1,1,0,0,0,[TimeSpan]::Zero))
        try { Save-Fixture "$variant.pem" $certificate.ExportCertificatePem() }
        finally { $certificate.Dispose() }
    }
    # Public offline test material: same existing server key, a deliberately known
    # test passphrase, and no deployment credentials. Wood must reject it, never prompt.
    $pbe = [Security.Cryptography.PbeParameters]::new([Security.Cryptography.PbeEncryptionAlgorithm]::Aes256Cbc,
        [Security.Cryptography.HashAlgorithmName]::SHA256, 1000)
    Save-Fixture 'encrypted-key.pem' $key.ExportEncryptedPkcs8PrivateKeyPem('wood-offline-test', $pbe)
}
if ($NativeVariantsOnly) {
    foreach ($name in @('ip.pem','cn-only.pem','encrypted-key.pem')) {
        if (Test-Path -LiteralPath (Join-Path $fixtureRoot $name)) { throw "Preserve existing fixture: $name" }
    }
    $key = [Security.Cryptography.RSA]::Create()
    try {
        $key.ImportFromPem([IO.File]::ReadAllText((Join-Path $fixtureRoot 'server-key.pem')))
        Save-NativeVariants $key
    } finally { $key.Dispose() }
    exit 0
}
function New-Root([string]$subject) {
    $key = [Security.Cryptography.RSA]::Create(3072)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        "CN=$subject", $key, [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($true, $true, 1, $true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyCertSign, $true))
    $certificate = $request.CreateSelfSigned([DateTimeOffset]::new(2020,1,1,0,0,0,[TimeSpan]::Zero), [DateTimeOffset]::new(2045,1,1,0,0,0,[TimeSpan]::Zero))
    return @{ Key = $key; Certificate = $certificate }
}
$root = New-Root 'Wood offline test CA'
$other = New-Root 'Wood unrelated test CA'
Save-Fixture 'ca.pem' $root.Certificate.ExportCertificatePem()
Save-Fixture 'wrong-ca.pem' $other.Certificate.ExportCertificatePem()
Save-Fixture 'wrong-key.pem' $other.Key.ExportPkcs8PrivateKeyPem()
foreach ($name in @('server', 'expired')) {
    $key = [Security.Cryptography.RSA]::Create(2048)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=localhost', $key, [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
    $usage = [Security.Cryptography.OidCollection]::new()
    $usage.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usage,$true))
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $san.AddDnsName('localhost')
    $request.CertificateExtensions.Add($san.Build())
    $endYear = if ($name -eq 'expired') { 2025 } else { 2040 }
    $certificate = $request.Create($root.Certificate, [DateTimeOffset]::new(2024,1,1,0,0,0,[TimeSpan]::Zero), [DateTimeOffset]::new($endYear,1,1,0,0,0,[TimeSpan]::Zero), [Security.Cryptography.RandomNumberGenerator]::GetBytes(16))
    Save-Fixture "$name.pem" $certificate.ExportCertificatePem()
    Save-Fixture "$name-key.pem" $key.ExportPkcs8PrivateKeyPem()
    Save-Fixture "$name-chain.pem" ($certificate.ExportCertificatePem() + "`n" + $root.Certificate.ExportCertificatePem())
    if ($name -eq 'server') { Save-NativeVariants $key }
    $certificate.Dispose()
    $key.Dispose()
}
Save-Fixture 'malformed.pem' 'invalid certificate'
$root.Certificate.Dispose()
$root.Key.Dispose()
$other.Certificate.Dispose()
$other.Key.Dispose()
