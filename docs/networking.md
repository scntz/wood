# Native streams, TCP and HTTP

These implemented APIs require no Python or third-party networking library. wood
supplies copying loops and HTTP policy. Both C++23 and LLVM-native implement the
socket/stream primitives; the LLVM Linux path uses its explicit initialized glibc
service profile for DNS and worker TLS, while ordinary native programs and the
compiler remain freestanding. See [native dependencies](native-cli.md#optional-service-dependencies).
Operations are synchronous; [std.task](backend.md#native-tasks) provides
joinable threads. Async syntax and an event loop are not implemented.

## Lifetime

Use `with expression as name:` to close a `stream` on every control-flow exit.
Copies share one identity and cursor; closing any alias invalidates all aliases.
Returning a scoped handle returns a closed handle. Return an unscoped handle when
transferring lifetime responsibility to a caller. Handles work inside records and
containers, but cannot be converted to JSON or passed through the scalar C ABI.
Concurrent operations on one handle are unsupported.

Explicit close is idempotent and reports failure. Final-handle destruction also
releases resources, but cannot report close/flush errors. A scope reports cleanup
errors on normal completion/return/loop control; during error/exit unwinding it
preserves the primary failure. See [the language contract](language.md#scoped-native-resources).
Forced process termination does not guarantee cleanup. This is not a filesystem
sandbox or a crash-durability/transaction contract.

## `std.io`

Strings are bytes; no newline/encoding conversion occurs, including on Windows.
Use `std.utf8` explicitly for text. Native errors use R4016; wood loop bounds and
early EOF use R4010.

| Function | Contract |
| --- | --- |
| `open(path: str, mode: str) -> stream` | `r`: read existing; `w`: create/truncate; `a`: create/append; `x`: exclusive creation. No `+` modes. UTF-8 Windows paths/native POSIX bytes; NUL rejected. |
| `read(source: stream, count: int) -> str` | Up to count bytes; short reads are normal. Empty means EOF unless count=0. Buffered line data is consumed first. Count: 0..16777216. |
| `line(source: stream, limit: int) -> str` | Includes LF (counted in limit), or final bytes before EOF; empty means EOF. Oversized lines fail, without promising line-boundary recovery. Limit: 0..16777216. |
| `read_exact(source: stream, count: int) -> str` | Exactly count bytes, or error on early EOF. Count: 0..16777216. |
| `read_all(source: stream, limit: int) -> str` | Reads through EOF within limit, probing one extra byte to detect excess. Limit: 0..16777216. |
| `write(destination: stream, data: str) -> void` | All bytes or failure; retrying can duplicate a prefix. File output is buffered. |
| `flush(destination: stream) -> void` | Flush writable file userspace buffers; no fsync/durability promise. Sockets reject flush. |
| `seek(source: stream, offset: int) -> void` | Absolute nonnegative file byte position; clears EOF/read-ahead. Platform offset limits apply. |
| `tell(source: stream) -> int` | Logical file position, accounting for line read-ahead. |
| `close(source: stream) -> void` | Idempotent; invalidates all aliases, including on close failure. |
| `closed(source: stream) -> bool` | Tests closed state without I/O. |
| `copy(source: stream, destination: stream, limit: int) -> int` | 64 KiB chunks through EOF; nonnegative int64 total budget. Excess probe fails. Returns copied bytes. |

File operations follow OS access/symlink rules, permissions and umask, and can
block on special files. Seek/tell fail on sockets/nonseekable files. Failures can
consume input or leave partial output. `read_exact`, `read_all`, and `copy` are
wood implementations. There is no atomic replacement or file cancellation API.

```sh
wood examples/stream_copy.wd -- input.bin new-output.bin 1073741824
```

The example refuses existing destinations and copies files larger than the
single-read allocation cap. A failed copy can leave an incomplete new output.

## `std.net`

| Function | Contract |
| --- | --- |
| `connect(host: str, number: int, timeout_ms: int) -> stream` | TCP to port 1..65535; numeric IPv4/IPv6 or OS host resolution. Up to 64 addresses are family-interleaved and raced with a 250 ms stagger under one deadline; first success wins and other attempts close. |
| `listen(address: str, number: int, timeout_ms: int) -> stream` | Explicit numeric bind address, port 0..65535 (0 selects a free port). IPv6 listeners are IPv6-only. |
| `accept(listener: stream) -> stream` | Wait for one connection, inheriting listener timeout. |
| `port(socket: stream) -> int` | Bound local port, including selected ephemeral ports. |
| `set_timeout(socket: stream, timeout_ms: int) -> void` | Future operation timeout, 1..86400000 ms. |
| `shutdown_write(socket: stream) -> void` | Idempotent half-close: peer receives EOF after sent bytes; local reads remain available. |

Use `std.io` for connection byte I/O and close. Listeners reject byte I/O. R4017
identifies socket/argument failures; R4018 identifies timeouts. Native nonblocking
sockets use readiness waits and monotonic deadlines for each logical
connect/accept/read/line/write; interruption/would-block retries do not reset them.
Buffered reads/EOF and zero-byte reads need not wait. Multi-operation wood helpers
apply the timeout to each read/write, rather than the entire helper loop.

**DNS follows OS resolver policy and is outside the connect deadline.** Use numeric
addresses when that phase must be bounded. Connection racing uses the idea from
[RFC 8305](https://www.rfc-editor.org/rfc/rfc8305.html); asynchronous DNS and the
full RFC algorithm are not implemented. Deadlines bound waits/retries, not OS scheduling delays.
Timeouts can consume a prefix; close a protocol connection after framing failure.
POSIX sends suppress SIGPIPE. Files/sockets are configured not to inherit into
child executables. Task cancellation is cooperative: socket waits/retries observe
the request and raise R4022 (`socket operation cancelled`). Readiness waits use
bounded slices; this does not preempt synchronous DNS, arbitrary OS calls or
foreign code, or promise a wall-clock bound under scheduling contention. Buffered
reads, EOF and empty writes may finish without waiting. UDP and asynchronous
preemption are not implemented; use
[std.tls/std.https](backend.md#tls-and-https) for optional OpenSSL 3 transport.

## `std.http`

The wood library implements a bounded **connection-close endpoint profile** using
[HTTP framing, RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) and
[HTTP semantics, RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html). It is not a
complete HTTP conformance claim, proxy, browser or web framework.

Public positional records are `Header(name: str, value: str)`,
`Limits(header_bytes: int, body_bytes: int, timeout_ms: int)`,
`Request(method: str, target: str, headers: list[Header], body: str)` and
`Response(status: int, reason: str, headers: list[Header], body: str)`.

| Function | Contract |
| --- | --- |
| `get(host: str, port: int, target: str, limits: Limits) -> Response` | GET using a fresh scoped TCP connection. |
| `request(host: str, port: int, message: Request, limits: Limits) -> Response` | Send method/target/headers/body; manage Host, Content-Length and Connection. |
| `read_request(connection: stream, limits: Limits) -> Request` | One HTTP/1.1 origin-form request, exactly one nonempty Host. |
| `read_response(connection: stream, method: str, limits: Limits) -> Response` | HTTP/1.0 or 1.1; up to eight informational responses; HEAD/204/304 have no body. |
| `write_response(connection: stream, message: Response, head_only: bool, limits: Limits) -> void` | Final 200..599 except 304. HEAD omits body but retains representation length; 204 forbids body/framing. |
| `header_values(headers: list[Header], name: str) -> list[str]` | Case-insensitive lookup retaining separate duplicate fields (including Set-Cookie). |
| `request_head(host: str, port: int, message: Request, limits: Limits) -> str` | Validates an outgoing request and returns its managed-framing header bytes without opening a connection or sending data. Used by HTTP and HTTPS before connecting. |
| `send_request(connection: stream, host: str, port: int, message: Request, limits: Limits) -> Response` | Validates/writes a request and reads its response on a caller-owned connected TCP or TLS stream. Updates stream timeouts, with separate write/read deadlines. Does not connect, close or transfer ownership; the caller must close after failure. |

Input normalizes names to lowercase and trims surrounding SP/HTAB. It requires
CRLF and rejects control injection, invalid/folded field names, whitespace before
colons, duplicate framing/Host fields (even identical), invalid/overflowing lengths
and simultaneous Transfer-Encoding/Content-Length. Supported body framing is
Content-Length, chunked (validated extensions, empty trailers), and EOF-delimited
responses. Requests without framing have no body. Truncation fails.

Outgoing headers cannot override Host, Content-Length, Transfer-Encoding,
Connection, Trailer, Expect or Upgrade. Caller input is validated before sending.
Limits: head 4..1048576 bytes including start line/CRLF; body 0..16777216 bytes;
timeout 1..86400000 ms; at most 128 input fields. Chunk metadata has a separate
head-sized budget. Each informational/final head has its own budget. Bodies are
materialized within the limit. A single monotonic deadline spans reading a message
(including informational heads/fragmented bodies); writing has its own deadline.
`request` has separate connect/write/read deadlines, excluding DNS. HTTP updates
the stream's timeout; do not share it with unrelated operations.

Profile/protocol errors use R4010 with an `HTTP:` prefix. Native line limits can
use R4016; socket errors/timeouts retain R4017/R4018. Close after any failure;
consumed input/partial output cannot be rolled back. Generic `std.io` remains
available for lower-level streaming protocols.

Authentication, redirects, URL/IDNA conversion, cookies, compression,
keep-alive/pooling, CONNECT, upgrades, Expect requests, nonempty trailers and proxy
forwarding are unsupported. Targets start with `/` and use visible escaped ASCII
without fragments. Plain HTTP provides no transport confidentiality/authentication;
there is no HTTPS URL fallback. [std.tls and std.https](backend.md#tls-and-https)
provide explicit verified TLS using optional OpenSSL 3. Native SQLite and joinable
task/socket handoff are described in [backend.md](backend.md).

Run [the JSON service](../examples/http_json_server.wd) and
[client](../examples/http_get.wd) in separate terminals:

```sh
wood examples/http_json_server.wd -- 127.0.0.1 8080 0
wood examples/http_get.wd -- 127.0.0.1 8080 /health
curl -H 'Content-Type: application/json' -d '[1,2,3]' http://127.0.0.1:8080/sum
```

The service supports GET/HEAD `/health` and POST `/sum`, handles bad JSON/framing,
and exposes no source paths in public errors. It is sequential, unauthenticated
and idle-time bounded, and is not a production internet deployment configuration.
