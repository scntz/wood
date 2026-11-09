# Maintainer helper: keep the capability inventory synchronized with registration.
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$metadataPath = Join-Path $repositoryRoot 'docs/language.json'
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json -AsHashtable
$registration = Get-Content -LiteralPath (Join-Path $repositoryRoot 'include/wood/builtins.hpp') -Raw
$metadata.builtins = @([regex]::Matches($registration, '\{"([^"]+)",') | ForEach-Object { $_.Groups[1].Value })
$metadata.types = @($metadata.types | Where-Object { $_ -notin @('database','statement','transaction','tls_context','task') }) + @('database','statement','transaction','tls_context','task')
$metadata.standard_modules = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'stdlib') -Filter '*.wd' | Sort-Object Name | ForEach-Object { 'std.' + $_.BaseName })
$metadata.not_implemented = @($metadata.not_implemented | Where-Object { $_ -notin @('threads','TLS/HTTPS','database drivers') })
$metadata.resources.types = @('stream','database','statement','transaction','tls_context','task')
$metadata.resources.transaction_cleanup = 'rollback unless committed; nested savepoints finish in reverse order'
$metadata.resources.task_cleanup = 'cooperative cancel and blocking join; wait required to observe worker errors'
$metadata.networking.tls = $true
$metadata.networking.cancellation = 'cooperative socket waits/retries: R4022; no preemption of OS DNS or arbitrary calls'
$metadata.networking.tls_details = [ordered]@{
    library = 'optional OpenSSL major 3'; minimum_version = 'TLS 1.2'; peer_verification = 'required client trust chain, validity, purpose and DNS/IP SAN'
    error_code = 'R4021'; trust_store = 'explicit PEM file or OpenSSL default roots'; insecure_mode = $false
    alpn = $false; revocation_checks = $false; directory_variable = 'WOOD_OPENSSL_DIR'
    close = 'close_notify, TCP write half-close, deadline-bound receive drain; unwinding aborts'
    fatal_error = 'connection unusable; subsequent I/O rejected; close releases without TLS shutdown'
}
$metadata.ffi.parameters = @('int64_t','double','borrowed str -> const char*, int64_t byte_length','borrowed synchronous scalar function pointer')
$metadata.ffi.callbacks = 'same thread, call duration only; first exception deferred until foreign return; zero/void fallback in C'
$metadata.sqlite = [ordered]@{
    module = 'std.sqlite'; minimum_version = '3.38.0'; library = 'optional system SQLite or WOOD_SQLITE_DIR'
    resources = @('database','statement','transaction'); thread_ownership = 'creating thread'; parameters = 'explicit copied bindings, 1-based'
    columns = 'strict kinds; 0-based'; statements = 'exactly one; no NUL'; transaction = 'nested savepoints; rollback on scope exit unless committed'
    limits = [ordered]@{ sql_bytes=1048576; value_bytes=16777216; columns=256; parameters=1000; expression_depth=128; transaction_depth=64 }
    query = 'bounded JSON rows; duplicate columns and blobs rejected'; error_code = 'R4020'; python_required = $false
    windows_architecture_bits = 64
}
$metadata.tasks = [ordered]@{
    module = 'std.task'; execution = 'joinable native threads'; message_type = 'immutable json'; socket_transfer = 'consumes connection; invalidates all sender aliases'
    maximum_active = 256; cancellation = 'cooperative stop token; SQLite progress and socket wait/retry checks; no forced I/O interruption'
    close = 'cancel and join; may block indefinitely'; wait = 'bounded, repeatable result; original worker failure rethrown'
    error_code = 'R4022'; async_syntax = $false; shared_mutable_state = $false; python_required = $false
}
$metadata | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $metadataPath -Encoding utf8NoBOM
