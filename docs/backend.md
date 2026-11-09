# Backend services

This page covers SQLite, TLS and HTTPS, tasks, and calling C with borrowed data and callbacks. None of them needs Python.
They're the pieces for building a service, not a web framework: there's no routing, authentication or migrations.

## Native dependencies

Programs load SQLite and OpenSSL only when they use them. Databases need **SQLite 3.38 or newer**, built thread-safe.
Windows uses the system `winsqlite3.dll`, and Linux `libsqlite3.so.0` (then `libsqlite3.so`). An older library gives
R4020. `WOOD_SQLITE_DIR` can name an absolute, trusted directory holding `sqlite3.dll` on Windows or the Linux library.

TLS needs **OpenSSL 3**: `libcrypto.so.3` and `libssl.so.3` on Linux, `libcrypto-3-x64.dll` and `libssl-3-x64.dll` on
Windows. On Windows, wood looks only in System32, never in the working directory or PATH, so you'll usually set
`WOOD_OPENSSL_DIR` to an absolute, trusted directory, which also supplies the DLLs those depend on. Linux uses the dynamic
loader's normal search. These variables, library directories and OpenSSL's own configuration are trusted, so keep them
under your control, and keep the libraries patched: wood never downloads or updates them.

wood doesn't ship these libraries. If you distribute a program that uses them, include them with their licenses. When
compiling emitted C++ yourself, add `-pthread`, and `-ldl` on Linux; `wood` and the CMake targets do this for you.

## SQLite

Both backends support this API. On Linux, a database program built with LLVM starts through glibc's loader; see
[optional service dependencies](native-cli.md#optional-service-dependencies).

```wood
import std.sqlite as sql
with sql.open(":memory:", 5000) as db:
    sql.execute(db, "CREATE TABLE IF NOT EXISTS notes(name TEXT UNIQUE)", [])
    with sql.begin(db) as scope:
        sql.execute(db, "INSERT INTO notes(name) VALUES(?)", [json("native")])
        sql.commit(scope)
    print(json_dump(sql.query(db, "SELECT name FROM notes", [], 100, 65536)))
```

`database`, `statement` and `transaction` are native handle types. Copies share
identity; `with` closes the original resource on every control-flow exit.
Closing a database finalizes all its statements, invalidates aliases and rolls back
open transactions. Open another connection inside each task; connections/statements
belong to their creating thread. Resource values cannot be converted to JSON.

| wood API | Contract |
| --- | --- |
| `sql.open(path: str, timeout_ms: int) -> database` | Read/write/create, no URI parsing; `:memory:` is supported; timeout 1..86400000 ms |
| `close(database)`, `closed(database) -> bool`, `version() -> str` | Idempotent close, identity state, loaded SQLite version |
| `prepare(database, sql: str) -> statement` | Exactly one nonempty statement; trailing whitespace/comments allowed; NUL/invalid UTF-8 rejected |
| `execute(database, sql, parameters: list[json]) -> int` | Bind JSON scalars (bool becomes 0/1), step without result rows, return last change count; use `query` for rows |
| `query(database, sql, parameters, row_limit: int, byte_limit: int) -> list[dict[str,json]]` | Unique column names; int/float/text/null; rejects blobs, excess rows/bytes; byte limit counts the sum of serialized row objects, excluding outer array separators |
| `begin(database) -> transaction`, `commit(transaction)`, `rollback(transaction)` | Nested SQLite savepoints, strict reverse completion order; scope exit rolls back unless committed |
| `changes(database) -> int`, `last_id(database) -> int` | SQLite connection-local `changes64` and last inserted rowid semantics |

Native statement APIs expose binary data and streaming rows without JSON:

| Builtin | Contract |
| --- | --- |
| `stmt_bind_int(s, index, int)`, `stmt_bind_float(s, index, float)` | 1-based parameter index; int64 preserved; finite floats only |
| `stmt_bind_text(s, index, str)`, `stmt_bind_blob(s, index, str)`, `stmt_bind_null(s, index)` | SQLite copies bytes; text validates UTF-8 and preserves embedded NUL; blob preserves arbitrary bytes |
| `stmt_step(s) -> bool` | True for a current row, false for completion; repeated completion stays false; every parameter must be explicitly bound |
| `stmt_reset(s)`, `stmt_close(s)` | Reset clears bindings and prior failure; close invalidates aliases |
| `stmt_columns(s) -> int`, `stmt_name(s, column) -> str` | Column indexes are 0-based; metadata is available before stepping |
| `stmt_kind(s, column) -> str` | `int`, `float`, `str`, `blob`, `null`; requires a current row |
| `stmt_int`, `stmt_float`, `stmt_text`, `stmt_blob` | Strict extraction; float also accepts integer columns; wrong kinds, invalid text and nonfinite numeric results are errors |

Use dense `?` positional parameters. Numbered gaps count as unbound parameters too.
Bind user data; SQL strings remain trusted application code. Parameters cannot bind
table names or SQL syntax. Preparing/executing statements can have side effects
(including PRAGMA and INSERT RETURNING); a failed `execute` is not automatic rollback.
Use a transaction for an atomic operation. Do not mix manual transaction-control SQL
with scoped transactions. Nested commit releases its savepoint; an enclosing rollback
still undoes its writes. Only the outermost release commits the transaction.

Limits per connection: values/rows 16 MiB, SQL 1 MiB, columns 256, expression depth
128, parameters 1000, and scoped transaction nesting 64. JSON query limits are
0..100000 rows and 0..16 MiB serialized row bytes. A single row must be materialized
before its JSON byte budget can be checked. Prepared statements are bound to the
connection and are not a global cache. Foreign keys are enabled; extension loading
is disabled, defensive mode is enabled and trusted schema is disabled.

The busy timeout bounds lock waiting. A monotonic progress handler checks the same
timeout and task cancellation every 1000 SQL VM instructions per step. This is not
a hard wall-clock interrupt: OS I/O, preparing SQL, busy waiting and individual VM
operations can delay it. SQLite errors use R4020. Rollback/close cannot guarantee
crash durability; filesystem, journal mode and deployment policy still matter.

## TLS and HTTPS

```wood
import std.http as http
import std.https as https
reply = https.get("example.com", 443, "/", http.Limits(16384, 1048576, 5000), "")
print(reply.status)
```

`std.tls.connect(host, port, ca_file, timeout_ms) -> stream` establishes TCP then
verified TLS. `ca_file` is a PEM trust bundle; an empty string uses **OpenSSL's**
default trust paths, which need not include the Windows/macOS system trust store.
An empty/unavailable trust store fails verification; there is no insecure mode or
plaintext fallback. DNS follows OS resolver policy; TCP connection and TLS handshake
each have their own timeout. Later stream operations retain their existing deadlines.

Verification requires a trusted chain, validity period, server certificate purpose
and matching DNS/IP SAN. DNS names must be ASCII (IDNA conversion is not provided);
SNI is sent for DNS names. Partial wildcards and subject-CN fallback are disabled.
Minimum TLS is 1.2, OpenSSL security level is 2, compression/renegotiation are disabled.
No automatic revocation checking, client certificates, certificate pinning API,
TLS session cache, ALPN negotiation, HTTP/2 or HTTP/3 is provided.

`std.tls.server(chain_file, private_key_file) -> tls_context` loads a leaf-first
certificate chain and unencrypted PEM key, checking that they match. Files must
contain 1..1048576 bytes; certificate bundles contain at most 512 certificates.
Encrypted certificate/key input is rejected without invoking an interactive
password callback, including legacy encrypted PEM envelopes.
Protect private-key files with OS permissions. `std.tls.accept(plain, context) -> stream`
consumes an unread full-duplex connected socket, invalidates all plain aliases,
and returns an ordinary stream backed by TLS. Buffered plaintext and repeated TLS
upgrades are rejected. Closing the context prevents further accepts; existing TLS
connections retain OpenSSL's context reference. `tls.close(context)` is idempotent;
`tls.version()` identifies the loaded library.

TLS reads/writes use nonblocking sockets and retry under one logical-operation
deadline. Native BIO callbacks preserve Windows socket width and POSIX SIGPIPE
policy. Clean close_notify becomes EOF; unexpected TCP truncation is R4021.
After a fatal TLS protocol/I/O error the connection is unusable: further I/O is
rejected and close releases its resources without calling TLS shutdown. There is
no plaintext retry. Nonfatal would-block/deadline/cancellation paths retain their
documented checked-error behavior; close resources after any protocol failure.
Normal stream close sends close_notify, half-closes TCP and drains incoming bytes
until peer TCP EOF under the socket timeout. This prevents unread trailing TLS
records from causing a reset that discards the response. Close may wait for the
peer to close its write side and raises R4018 if the deadline expires; close the
client connection after receiving its response/EOF before waiting for a server task
to finish. Discarded close-drain bytes are never exposed as application data.
`net.shutdown_write` sends close_notify and half-closes only; it does not drain.
Unwinding aborts transport cleanup to preserve the primary
error. Timeouts retain R4018; TLS configuration/protocol/verification errors use R4021.

`std.https.request(host, port, http.Request, http.Limits, ca_file)` and `get` use
the same framing/limits as [HTTP](networking.md). `http.send_request(connection,
host, port, message, limits)` works on an existing stream; `http.request_head`
validates/builds the outgoing header without I/O. Applications close the connection
after each message. HTTP request/response bodies remain whole bounded byte strings.

## Native tasks

```wood
import std.task as tasks
fn square(message: json) -> json:
    n = json_int(message)
    return json(n * n)
with tasks.spawn(square, json(12)) as work:
    assert(json_int(tasks.wait(work, 5000)) == 144)
```

| API | Contract |
| --- | --- |
| `spawn(fn(json)->json, payload: json) -> task` | New native thread, immutable JSON payload/result, at most 256 actively running tasks |
| `spawn_io(fn(stream,json)->json, connection: stream, payload: json) -> task` | Consumes a connected plain/TLS socket; all caller aliases become closed; worker owns and scope-closes the connection |
| `wait(task, timeout_ms: int) -> json` | Timeout 1..86400000; returns the same result on repeated waits; rethrows original worker failure |
| `done(task) -> bool`, `cancel(task)`, `cancelled() -> bool` | Completion polling, cooperative stop request, current worker's stop status |
| `checkpoint() -> void` | Raise R4022 with `task cancelled` when the current worker has a stop request; otherwise return normally. No effect on the main thread. |
| `close(task)` | Idempotent cancel-and-join; invalidates aliases; `with` performs this automatically |

Functions have no captured variables and JSON has immutable shared backing, so
tasks cannot share mutable wood state. Open independent databases/files inside
workers. Successful socket handoff moves ownership, buffering and TLS state;
allocation, capacity and OS thread-start failures preserve the sender connection.
Tasks are not detached. Closing a task
can block indefinitely if its callback ignores cancellation; `wait` timeout does
not stop it. `std.time.sleep`, network waits and foreign code are not forcibly
interrupted by task cancellation. Use throwing `checkpoint()` calls or poll
`cancelled()` in computation loops, and use
bounded I/O. SQLite progress checks cancellation. Close joins but does not rethrow
worker errors: call `wait` to observe them. Worker `exit` is rethrown on wait and
exits the waiting program; it is not an isolated subprocess exit.

Tasks give you threads, not async syntax, an event loop, channels or coroutines. Task errors use R4022. Both backends
behave the same, including `spawn_io`, and on both, a worker's scope finishes before its resources are released.

## Borrowed C ABI

`extern fn checksum(data: str) -> int` expands to C
`int64_t checksum(const char* data, int64_t byte_length)`. The immutable bytes may
contain NUL and are valid only during the call. No terminator is promised. Foreign
code must not retain, modify or free them. Owned string returns are not supported.

External parameters may also be `fn(int/float, ...) -> int/float/void`. These are
borrowed synchronous callbacks on the calling thread. Generated trampolines save
the first thrown failure, return a zero/void fallback to C, skip subsequent callback
invocations after failure, and rethrow once the foreign function returns. Foreign
side effects before that point remain. Reentrant calls restore prior callbacks.
The C library must not retain callbacks or call them on another thread; such use
violates the ABI contract. Trusted native code is not sandboxed. See
[the real static-library example](../examples/ffi.wd). Raw pointers, aggregates,
variadics, dynamic foreign declarations and retained callbacks remain unsupported.

## End-to-end service

[backend.wd](../examples/backend.wd) implements a finite-run HTTPS JSON service:

```sh
wood build examples/backend.wd -o backend
./backend application.db chain.pem private-key.pem 8443 100 ready.txt
curl --cacert ca.pem https://localhost:8443/health
curl --cacert ca.pem -H 'Content-Type: application/json' \
  -d '{"names":["first","second"]}' https://localhost:8443/items
curl --cacert ca.pem https://localhost:8443/items
```

Use your deployment certificates and trust bundle. Repository test identities are
public fixtures, not credentials. The example binds loopback, accepts the specified
number of connections, bounds worker batches at eight, validates names and uses
prepared inserts within one transaction per batch. A duplicate name rolls the
entire batch back. SQLite data persists across restarts. It demonstrates overlapping
requests, encrypted transport, safe data binding and scoped cleanup, but supplies
no authentication, authorization, pagination beyond the first 1000 items, graceful
signal-driven shutdown, migrations, metrics, deployment management or overload
queue. Those are up to the application.

The implementation follows the [SQLite C API](https://www.sqlite.org/c3ref/intro.html), the
[OpenSSL 3 API](https://docs.openssl.org/3.0/man3/SSL_CTX_set_verify/) and
[HTTP's guidance on closing connections](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.6).
