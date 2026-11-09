param([Parameter(Mandatory)][string]$Server,
      [Parameter(Mandatory)][string]$Fixtures,
      [Parameter(Mandatory)][string]$Work,
      [switch]$JsonService)
$ErrorActionPreference = 'Stop'
# An independent .NET TLS client: no Wood/OpenSSL client, no validation callback,
# no system trust-store changes. Only this connection trusts the offline test CA.
$runDirectory = Join-Path $Work ([Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$ready = Join-Path $runDirectory 'ready.txt'
$authority = [Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem(
    [IO.File]::ReadAllText((Join-Path $Fixtures 'ca.pem')))
$start = [Diagnostics.ProcessStartInfo]::new($Server)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$serverArguments = if ($JsonService) {
    @('127.0.0.1', '0', (Join-Path $Fixtures 'server-chain.pem'),
      (Join-Path $Fixtures 'server-key.pem'), '3', $ready)
} else {
    @((Join-Path $runDirectory 'items.db'), (Join-Path $Fixtures 'server-chain.pem'),
      (Join-Path $Fixtures 'server-key.pem'), '0', '3', $ready)
}
foreach ($argument in $serverArguments) {
    $start.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
function Request([int]$port, [string]$method, [string]$target, [string]$body, [int]$expected) {
    $socket = [Net.Sockets.TcpClient]::new('localhost', $port)
    $secure = [Net.Security.SslStream]::new($socket.GetStream(), $false)
    try {
        $secure.ReadTimeout = 5000
        $secure.WriteTimeout = 5000
        $options = [Net.Security.SslClientAuthenticationOptions]::new()
        $options.TargetHost = 'localhost'
        $options.CertificateChainPolicy = [Security.Cryptography.X509Certificates.X509ChainPolicy]::new()
        $options.CertificateChainPolicy.TrustMode = [Security.Cryptography.X509Certificates.X509ChainTrustMode]::CustomRootTrust
        $options.CertificateChainPolicy.CustomTrustStore.Add($authority) | Out-Null
        $options.CertificateChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $options.CertificateChainPolicy.ApplicationPolicy.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
        $secure.AuthenticateAsClient($options)
        $bodyBytes = [Text.Encoding]::UTF8.GetBytes($body)
        $headBytes = [Text.Encoding]::ASCII.GetBytes("$method $target HTTP/1.1`r`nHost: localhost`r`nContent-Length: $($bodyBytes.Length)`r`n`r`n")
        $secure.Write($headBytes, 0, $headBytes.Length)
        # Keep the empty write: it exposed the Schannel close/reset regression.
        $secure.Write($bodyBytes, 0, $bodyBytes.Length)
        $output = [IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(16384)
            try {
                while (($received = $secure.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $output.Write($buffer, 0, $received)
                    if ($output.Length -gt 1048576) { throw 'Independent TLS response exceeded budget' }
                }
            } catch {
                throw "Independent TLS read failed after $($output.Length) bytes: $($_.Exception.Message)"
            }
            $text = [Text.Encoding]::UTF8.GetString($output.ToArray())
        } finally { $output.Dispose() }
        if (-not $text.StartsWith("HTTP/1.1 $expected ")) { throw "Unexpected HTTPS response: $text" }
        $separator = $text.IndexOf("`r`n`r`n")
        if ($separator -lt 0) { throw 'Missing HTTP header terminator' }
        return $text.Substring($separator + 4) | ConvertFrom-Json
    } finally {
        $secure.Dispose()
        $socket.Dispose()
    }
}
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $readyText = ''
    while (-not $readyText.EndsWith("`n")) {
        if ($process.HasExited -or $timer.Elapsed.TotalSeconds -gt 10) { throw 'Backend did not start' }
        if (Test-Path -LiteralPath $ready) {
            try { $readyText = [IO.File]::ReadAllText($ready) }
            catch [IO.IOException] {
                # File creation precedes the producer's exclusive close. Retry
                # only sharing violations, within the unchanged startup deadline.
                if (($_.Exception.HResult -band 0xffff) -ne 32) { throw }
            }
        }
        Start-Sleep -Milliseconds 10
    }
    $port = [int]$readyText.Trim()
    if ((Request $port 'GET' '/health' '' 200).status -ne 'ok') { throw 'Health JSON mismatch' }
    if ($JsonService) {
        if ((Request $port 'POST' '/sum' '[1,2,3]' 200).sum -ne 6) { throw 'Sum JSON mismatch' }
        if ((Request $port 'GET' '/absent' '' 404).error -ne 'route not found') { throw 'Missing route mismatch' }
    } else {
    if ((Request $port 'POST' '/items' '{"names":["independent TLS client"]}' 201).created -ne 1) { throw 'Insert JSON mismatch' }
    $rows = @(Request $port 'GET' '/items' '' 200)
    if ($rows.Count -ne 1 -or $rows[0].name -ne 'independent TLS client') { throw 'Persistent JSON mismatch' }
    }
    if (-not $process.WaitForExit(10000)) { throw 'Backend did not exit' }
    if ($process.ExitCode -ne 0) { throw "Backend failed: $($stderr.GetAwaiter().GetResult())" }
    if ($JsonService) { Write-Output 'Independent .NET TLS/HTTPS JSON service and clean shutdown passed' }
    else { Write-Output 'Independent .NET TLS/HTTPS client, SQLite persistence and clean shutdown passed' }
} finally {
    if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
    [IO.File]::WriteAllText((Join-Path $runDirectory 'server.stdout'), $stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $runDirectory 'server.stderr'), $stderr.GetAwaiter().GetResult())
    $process.Dispose()
    $authority.Dispose()
}
