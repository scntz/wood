# Standard modules

The standard library has 22 native modules, plus the experimental `std.python`. This page says what each one promises, which
errors it raises, and where to find tests and examples that use it. Exact signatures are in the generated
[API index](stdlib-api.md), and [the library guide](stdlib.md) explains the built-in functions.

## Names

Modules live under `std.`, and are usually imported with a short alias, such as `import std.num as num`. A few pairs look
similar but do different jobs:

- `std.num` holds array primitives, and `std.linalg` linear algebra and geometry on top of them.
- `std.collections` holds eager data structures, `std.functional` eager list algorithms that take the callback first, and
  `std.iter` lazy iterators that take the source first.

Functions such as `io.open`, `sqlite.open` and `env.get` are ordinary namespaced functions, not reserved words, and a
function of your own with the same name always wins over a built-in.

All 22 modules work on Windows and Linux x86-64 with both backends, and none needs Python. TLS, HTTPS and SQLite need their
[optional libraries](installation.md#optional-dependencies).

## Modules

| Module | What it promises, and its main errors | Tests and examples |
| --- | --- | --- |
| [binary](stdlib.md#binary-integer-formats) | Little- and big-endian integers of 1 to 8 bytes, signed or unsigned. A bad width, order or range raises R4010 | [tests](../tests/binary.wd), [archive codec](../examples/archive/src/codec.wd) |
| [collections](generic-records.md#reusable-native-collections) | Stable integer sorting, search, and generic `Stack`, `Queue` and `Heap` with value semantics. Popping an empty one raises R4007. Comparators must be consistent | [tests](../tests/generic_records.wd), [edge cases](../tests/stdlib_contracts.wd), [route planner](../examples/route_planner.wd) |
| [csv](stdlib.md#csv) | Whole documents, a single-byte delimiter, quoted newlines. Bad quoting, delimiters, headers or row widths raise R4010. No encoding conversion or formula escaping | [tests](../tests/csv.wd), [configured report](../examples/configured_report.wd) |
| [env](stdlib.md#environment-configuration) | Read-only environment variables as `str` or `None`, with empty different from missing. Bad names raise R4003, OS errors R4008. Case-insensitive on Windows | [tests](../tests/environment.wd), [configured report](../examples/configured_report.wd) |
| [fs](stdlib.md#native-filesystem-and-paths) | Sorted directory listings with limits, and a walk that skips links. `remove` never deletes recursively. OS errors raise R4008, limit errors R4010 | [tests](../tests/native_services.wd), [tree report](../examples/tree_report.wd) |
| [functional](stdlib.md#reusable-native-algorithms-stdfunctional) | Eager list algorithms. Callbacks run in order and may raise. A fold over an empty list returns its initial value. Use `iter.find_first` to tell a found `None` from not found | [tests](../tests/functional.wd), [edge cases](../tests/stdlib_contracts.wd) |
| [geometry](native-computing.md#linear-algebra-and-geometry) | Meshes of `(n,3)` points and `(m,3)` triangles, transforms and normals. Degenerate geometry raises R4010, array errors R4026. Reads and writes [a subset of OBJ](obj.md) | [tests](../tests/obj_import.wd), [mesh import](../examples/mesh_import.wd) |
| [http](networking.md#stdhttp) | Synchronous HTTP/1.1 with one request per connection, strict framing, deadlines and size limits. Protocol errors raise R4010 | [tests](../tests/http.wd), [JSON server](../examples/http_json_server.wd), [client](../examples/http_get.wd) |
| [https](backend.md#tls-and-https) | HTTP over verified TLS, with the trust roots you supply. Never falls back to plain HTTP. TLS errors raise R4021 | [tests](../tests/native/tls.cmake), [client](../examples/https_client.wd) |
| [io](networking.md#stdio) | Binary streams whose aliases share a position and close together. `read_exact`, `read_all` and `copy` take limits. Mode `x` creates a file only if it doesn't exist | [tests](../tests/streams.wd), [file copy](../examples/stream_copy.wd) |
| [iter](closures-iterators.md#stditer-api) | Lazy iterators that own a snapshot of their source. Taking from an empty step raises R4024, and negative limits R4010. Infinite sources need `take` | [tests](../tests/closures_iterators.wd), [log processor](../examples/log_report.wd) |
| [linalg](native-computing.md#linear-algebra-and-geometry) | `Vec3`, affine matrices and pivoted linear solves. Zero-length normalization and singular systems raise R4010, shape errors R4026 | [tests](../tests/native_computing.wd), [edge cases](../tests/stdlib_numerical_edges.wd), [torus generator](../examples/mesh_torus.wd) |
| [math](stdlib.md#embedded-modules) | Float functions such as `sqrt`, `sin` and `pow`, and checked integer `gcd`, `lcm`, `pow_int` and `clamp_int`. `gcd(0, 0)` is 0. Bad arguments raise R4010, overflow R4001 | [tests](../tests/stdlib_contracts.wd) |
| [net](networking.md#stdnet) | TCP over IPv4 and IPv6, with DNS, deadlines and half-close. Socket errors raise R4017, timeouts R4018, cancellation R4022. Don't share one stream between threads | [tests](../tests/native/network.cmake), [HTTP server](../examples/http_json_server.wd) |
| [num](native-computing.md) | Fixed-shape int and float arrays with views, copy on write and reductions. Shape errors raise R4026, indexing R4006, math domain errors R4027, overflow R4001 | [tests](../tests/native_arrays.wd), [edge cases](../tests/stdlib_numerical_edges.wd), [grid mesh](../examples/native_grid_mesh.wd) |
| [path](stdlib.md#native-filesystem-and-paths) | Native paths, joined lexically or resolved through links. Errors raise R4008. A check doesn't stop the file changing before you open it | [tests](../tests/native_services.wd), [tree report](../examples/tree_report.wd) |
| [sqlite](backend.md#sqlite) | Bound parameters, statements, transactions that roll back on scope exit, and rows as JSON. Every parameter must be bound. SQLite errors raise R4020. Needs SQLite 3.38 or newer | [tests](../tests/sqlite.wd), [inventory](../examples/sqlite_inventory.wd) |
| [stats](stdlib.md#embedded-modules) | Checked integer sums, and mean and population variance computed in one pass. Empty input raises R4010, overflow R4001 | [tests](../tests/statistics_edges.wd), [edge cases](../tests/stdlib_contracts.wd) |
| [task](backend.md#native-tasks) | Joinable worker threads that exchange immutable JSON. `spawn_io` hands a socket to a worker. Leaving a scope cancels and joins. Lifecycle errors raise R4022, and a worker's own error is passed on | [tests](../tests/tasks.wd), [parallel word counts](../examples/parallel_word_counts.wd) |
| [time](stdlib.md#native-time) | Monotonic clocks and `sleep` for up to a day. Bad durations raise R4015. No dates, wall clock or time zones | [tests](../tests/time.wd), [timing](../examples/timing.wd) |
| [tls](backend.md#tls-and-https) | TLS through OpenSSL 3 with full certificate, hostname and SNI checks, and no way to turn them off. TLS errors raise R4021 | [tests](../tests/native/tls.cmake), [HTTPS server](../examples/https_json_server.wd) |
| [utf8](stdlib.md#bytes-and-utf-8) | Strict decoding and encoding, counting and slicing by character. Invalid encodings raise R4010. No normalization, graphemes or case folding | [tests](../tests/utf8.wd), [Unicode report](../examples/unicode_report.wd) |
| [python](python-distribution.md), experimental | Python objects behind explicit conversions, with the GIL taken for you. Errors raise R4030, R4031 and R4032. CPython 3.14 only, with no automatic NumPy conversion | [tests](../tests/native_python.wd), [NumPy example](../examples/python_numpy.wd) |

Any module can also raise the usual allocation, arithmetic, indexing, callback and I/O errors. Close resources explicitly or
with `with` when you need to see cleanup errors, since a destructor can't report them all. None of these modules is a
security sandbox, and writing a file doesn't guarantee it reached the disk.

`Heap` comparators are plain `fn` values, not closures. The algorithms in `std.functional` and `std.iter` accept closures.

## Not included

JSON, strings, formatting, arguments and standard input and output are [built in](stdlib.md), so they have no module.
There's no regex, Unicode tables or case folding, time zones or calendars, archives, UDP, async or event loops, launching
processes, or web or scientific frameworks. Some of these are covered by [official packages](official-packages.md).
