# Standard library API index

Generated from public declarations by `cmake -DUPDATE_API=ON -P tools/check-docs.cmake`.
Do not edit signatures here. [Library guide](stdlib.md) and linked contracts define
behavior, errors, costs and limits; this index supplies exact spelling and defaults.
Omitted return annotations mean inference, not necessarily void. Generic parameters
and associated types are resolved on specialization. These are declarations, not
complete runnable programs. Underscore declarations are private and excluded.
Record fields are shown for layout; prefer documented factories/methods to preserve
invariants. Unless explicitly marked LLVM-only, this is the default C++23 path's API, not an LLVM support promise.

## Modules

- [std.binary](#stdbinary) — Endian-explicit signed and nonnegative integer wire formats.
- [std.collections](#stdcollections) — Stable integer sorting/search and generic Stack, Queue and Heap.
- [std.csv](#stdcsv) — Whole-document quoted CSV parsing, serialization and header tables.
- [std.env](#stdenv) — Read-only owned environment values, distinguishing absence from empty strings.
- [std.fs](#stdfs) — Bounded filesystem listing, traversal and explicit creation/removal.
- [std.functional](#stdfunctional) — Eager callback-first type-safe collection algorithms.
- [std.geometry](#stdgeometry) — Triangle meshes, finite geometry, transforms and bounded OBJ I/O.
- [std.http](#stdhttp) — Bounded synchronous connection-close HTTP messages and endpoints.
- [std.https](#stdhttps) — Verified HTTPS over optional OpenSSL 3 streams.
- [std.io](#stdio) — Scoped binary file streams and bounded copying.
- [std.iter](#stditer) — Lazy source-first adapters, owned cursor protocols and consumers.
- [std.linalg](#stdlinalg) — Vec3 arithmetic, affine transforms and partial-pivoted linear solves.
- [std.math](#stdmath) — Checked integer GCD, LCM, power and clamping.
- [std.net](#stdnet) — Synchronous deadline-bound TCP connections/listeners and half-close.
- [std.num](#stdnum) — Fixed-shape typed numeric arrays, views, conversions and kernels.
- [std.path](#stdpath) — Host path assembly, canonical identities and link-aware containment.
- [std.python](#stdpython) — EXPERIMENTAL LLVM-only CPython 3.14 interoperability; explicit managed objects and conversions.
- [std.random](#stdrandom) — OS-backed cryptographic bytes and unbiased bounded integers; no insecure fallback.
- [std.sqlite](#stdsqlite) — Prepared statements, bounded JSON rows and rollback scopes; optional SQLite.
- [std.stats](#stdstats) — Checked integer totals and binary64 online/scaled statistics.
- [std.task](#stdtask) — Joinable threads, immutable JSON messages and explicit socket handoff.
- [std.time](#stdtime) — Blocking sleep and monotonic elapsed-time counters.
- [std.tls](#stdtls) — Verified TLS clients, server contexts and stream handoff; optional OpenSSL.
- [std.utf8](#stdutf8) — Strict Unicode scalar decoding/encoding over byte strings.

## std.binary

Endian-explicit signed and nonnegative integer wire formats.

[Behavior and limits](stdlib.md#binary-integer-formats) · [Source](../stdlib/binary.wd) · [Regression source](../tests/binary.wd)

```text
fn pack_int(value: int, width: int, order: str) -> str:
fn pack_uint(value: int, width: int, order: str) -> str:
fn unpack_int(text: str, order: str) -> int:
fn unpack_uint(text: str, order: str) -> int:
```

## std.collections

Stable integer sorting/search and generic Stack, Queue and Heap.

[Behavior and limits](generic-records.md#reusable-native-collections) · [Source](../stdlib/collections.wd) · [Regression source](../tests/generic_records.wd)

```text
fn sorted_int(values: list[int]) -> list[int]:
fn binary_search(values: list[int], needle: int) -> int:
record Stack[T]:
    items: list[T]
    fn push(mut self, value: T):
    fn pop(mut self) -> T:
    fn peek(self) -> T | None:
    fn size(self) -> int:
    fn clear(mut self):
record Queue[T]:
    incoming: list[T]
    outgoing: list[T]
    fn push(mut self, value: T):
    fn pop(mut self) -> T:
    fn peek(self) -> T | None:
    fn size(self) -> int:
    fn clear(mut self):
record Heap[T]:
    items: list[T]
    before: fn(T, T) -> bool
    fn push(mut self, value: T):
    fn pop(mut self) -> T:
    fn peek(self) -> T | None:
    fn size(self) -> int:
    fn clear(mut self):
fn heap[T](items: list[T], before: fn(T, T) -> bool) -> Heap[T]:
```

## std.csv

Whole-document quoted CSV parsing, serialization and header tables.

[Behavior and limits](stdlib.md#csv) · [Source](../stdlib/csv.wd) · [Regression source](../tests/csv.wd)

```text
fn parse(text: str, delimiter: str) -> list[list[str]]:
fn dump(rows: list[list[str]], delimiter: str) -> str:
fn read_table(text: str, delimiter: str) -> list[dict[str, str]]:
```

## std.env

Read-only owned environment values, distinguishing absence from empty strings.

[Behavior and limits](stdlib.md#environment-configuration) · [Source](../stdlib/env.wd) · [Regression source](../tests/environment.wd)

```text
fn get(name: str) -> str | None:
```

## std.fs

Bounded filesystem listing, traversal and explicit creation/removal.

[Behavior and limits](stdlib.md#native-filesystem-and-paths) · [Source](../stdlib/fs.wd) · [Regression source](../tests/native_services.wd)

```text
fn kind(path: str) -> str:
fn entries(path: str, limit: int) -> list[str]:
fn size(path: str) -> int:
fn make_dirs(path: str) -> bool:
fn remove(path: str) -> bool:
fn walk_files(root: str, max_entries: int) -> list[str]:
fn find_files(root: str, predicate: fn(str) -> bool, max_entries: int) -> list[str]:
```

## std.functional

Eager callback-first type-safe collection algorithms.

[Behavior and limits](stdlib.md#reusable-native-algorithms-stdfunctional) · [Source](../stdlib/functional.wd) · [Regression source](../tests/functional.wd)

```text
fn map[T, U, F](transform: F(T) -> U, items: list[T]) -> list[U]:
fn filter[T, F](predicate: F(T) -> bool, items: list[T]) -> list[T]:
fn fold[T, U, F](step: F(U, T) -> U, items: list[T], initial: U) -> U:
fn first[T](items: list[T]) -> T | None:
fn find_first[T, F](predicate: F(T) -> bool, items: list[T]) -> T | None:
fn zip[T, U](left: list[T], right: list[U]) -> list[tuple[T, U]]:
fn enumerate[T](items: list[T], start: int = 0) -> list[tuple[int, T]]:
fn partition[T, F](predicate: F(T) -> bool, items: list[T]) -> tuple[list[T], list[T]]:
fn lookup[K, V](items: dict[K, V], key: K) -> V | None:
```

## std.geometry

Triangle meshes, finite geometry, transforms and bounded OBJ I/O.

[Behavior and limits](native-computing.md#linear-algebra-and-geometry) · [Source](../stdlib/geometry.wd) · [Regression source](../tests/obj_import.wd)

```text
record Mesh:
    vertices: array[float]
    triangles: array[int]
fn validate(mesh: Mesh):
fn point(vertices: array[float], index: int):
fn transformed(mesh: Mesh, matrix: array[float]):
fn bounds(mesh: Mesh) -> tuple[la.Vec3,la.Vec3]:
fn area(mesh: Mesh) -> float:
fn normals(mesh: Mesh) -> array[float]:
fn torus(rings: int, sides: int, major: float = 2.0, minor: float = 0.5) -> Mesh:
fn write_obj(mesh: Mesh, path: str):
fn read_obj(path: str, *, max_bytes: int = 16777216, max_vertices: int = 1000000, max_triangles: int = 1000000, max_normals: int = 1000000, max_line_bytes: int = 4096) -> Mesh:
```

## std.http

Bounded synchronous connection-close HTTP messages and endpoints.

[Behavior and limits](networking.md#stdhttp) · [Source](../stdlib/http.wd) · [Regression source](../tests/http.wd)

```text
record Header:
    name: str
    value: str
record Limits:
    header_bytes: int
    body_bytes: int
    timeout_ms: int
record Request:
    method: str
    target: str
    headers: list[Header]
    body: str
record Response:
    status: int
    reason: str
    headers: list[Header]
    body: str
fn header_values(headers: list[Header], name: str) -> list[str]:
fn read_request(connection: stream, limits: Limits) -> Request:
fn read_response(connection: stream, method: str, limits: Limits) -> Response:
fn request_head(host: str, port: int, message: Request, limits: Limits) -> str:
fn send_request(connection: stream, host: str, port: int, message: Request, limits: Limits) -> Response:
fn request(host: str, port: int, message: Request, limits: Limits) -> Response:
fn get(host: str, port: int, target: str, limits: Limits) -> Response:
fn write_response(connection: stream, message: Response, head_only: bool, limits: Limits):
```

## std.https

Verified HTTPS over optional OpenSSL 3 streams.

[Behavior and limits](backend.md#tls-and-https) · [Source](../stdlib/https.wd) · [Regression source](../tests/backend.cmake)

```text
fn request(host: str, port: int, message: http.Request, limits: http.Limits, ca_file: str) -> http.Response:
fn get(host: str, port: int, target: str, limits: http.Limits, ca_file: str) -> http.Response:
```

## std.io

Scoped binary file streams and bounded copying.

[Behavior and limits](networking.md#stdio) · [Source](../stdlib/io.wd) · [Regression source](../tests/streams.wd)

```text
fn open(path: str, mode: str) -> stream:
fn read(source: stream, count: int) -> str:
fn line(source: stream, limit: int) -> str:
fn write(destination: stream, data: str):
fn flush(destination: stream):
fn close(source: stream):
fn closed(source: stream) -> bool:
fn seek(source: stream, offset: int):
fn tell(source: stream) -> int:
fn read_exact(source: stream, count: int) -> str:
fn read_all(source: stream, limit: int) -> str:
fn copy(source: stream, destination: stream, limit: int) -> int:
```

## std.iter

Lazy source-first adapters, owned cursor protocols and consumers.

[Behavior and limits](closures-iterators.md#stditer-api) · [Source](../stdlib/iter.wd) · [Regression source](../tests/closures_iterators.wd)

```text
record Lines:
    source: stream
    limit: int
    done: bool
    fn next(mut self) -> step[str]:
record Generate[F]:
    producer: F
    done: bool
    fn next(mut self) -> F.Result:
record Map[I,F]:
    source: I
    transform: F
    fn next(mut self) -> step[F.Result]:
record Filter[I,F]:
    source: I
    predicate: F
    fn next(mut self) -> step[I.Item]:
record Take[I]:
    source: I
    remaining: int
    fn next(mut self) -> step[I.Item]:
record Drop[I]:
    source: I
    remaining: int
    fn next(mut self) -> step[I.Item]:
record Enumerate[I]:
    source: I
    index: int
    first: bool
    fn next(mut self) -> step[tuple[int,I.Item]]:
record Zip[A,B]:
    left: A
    right: B
    exhausted: bool
    fn next(mut self) -> step[tuple[A.Item,B.Item]]:
record Chain[A,B]:
    left: A
    right: B
    left_done: bool
    fn next(mut self) -> step[A.Item]:
fn map[S,F](source: S, transform: F(S.Item)) -> Map[S.Iterator,F]:
fn from_fn[T,F](producer: F() -> step[T]) -> Generate[F]:
fn lines(source: stream, limit: int = 1048576) -> Lines:
fn filter[S,F](source: S, predicate: F(S.Item) -> bool) -> Filter[S.Iterator,F]:
fn take[S](source: S, amount: int) -> Take[S.Iterator]:
fn drop[S](source: S, amount: int) -> Drop[S.Iterator]:
fn enumerate[S](source: S, start: int = 0) -> Enumerate[S.Iterator]:
fn zip[A,B](left: A, right: B) -> Zip[A.Iterator,B.Iterator]:
fn chain[A,B](left: A, right: B) -> Chain[A.Iterator,B.Iterator]:
fn collect[S](source: S) -> list[S.Item]:
fn fold[S,A,F](source: S, initial: A, combine: F(A,S.Item) -> A) -> A:
fn count[S](source: S) -> int:
fn any[S,F](source: S, predicate: F(S.Item) -> bool) -> bool:
fn all[S,F](source: S, predicate: F(S.Item) -> bool) -> bool:
fn find_first[S,F](source: S, predicate: F(S.Item) -> bool) -> step[S.Item]:
```

## std.linalg

Vec3 arithmetic, affine transforms and partial-pivoted linear solves.

[Behavior and limits](native-computing.md#linear-algebra-and-geometry) · [Source](../stdlib/linalg.wd) · [Regression source](../tests/native_computing.wd)

```text
record Vec3:
    x: float
    y: float
    z: float
    fn plus(self, other: Vec3):
    fn minus(self, other: Vec3):
    fn scaled(self, amount: float):
    fn dot(self, other: Vec3) -> float:
    fn cross(self, other: Vec3):
    fn length(self) -> float:
    fn normalized(self):
fn identity(size: int) -> array[float]:
fn translation(x: float, y: float, z: float) -> array[float]:
fn scaling(x: float, y: float, z: float) -> array[float]:
fn rotation_x(angle: float) -> array[float]:
fn rotation_y(angle: float) -> array[float]:
fn rotation_z(angle: float) -> array[float]:
fn transform_points(points: array[float], matrix: array[float]) -> array[float]:
fn solve(matrix: array[float], right: array[float], tolerance: float = 0.000000000001) -> array[float]:
```

## std.math

Checked integer GCD, LCM, power and clamping.

[Behavior and limits](stdlib.md#embedded-modules) · [Source](../stdlib/math.wd) · [Regression source](../tests/stdlib_contracts.wd)

```text
fn gcd(a: int, b: int) -> int:
fn lcm(a: int, b: int) -> int:
fn pow_int(base: int, exponent: int) -> int:
fn clamp_int(value: int, low: int, high: int) -> int:
fn pi() -> float:
fn e() -> float:
fn radians(value: float) -> float:
fn degrees(value: float) -> float:
fn abs(value: float) -> float:
fn abs_float(value: float) -> float:
fn trunc(value: float) -> float:
fn floor(value: float) -> float:
fn ceil(value: float) -> float:
fn round(value: float) -> float:
fn exp(value: float) -> float:
fn log(value: float) -> float:
fn log2(value: float) -> float:
fn log10(value: float) -> float:
fn pow(base: float, power: float) -> float:
fn hypot(a: float, b: float) -> float:
fn sqrt(value: float) -> float:
fn sin(value: float) -> float:
fn cos(value: float) -> float:
fn tan(value: float) -> float:
fn atan(value: float) -> float:
fn atan2(y: float, x: float) -> float:
fn asin(value: float) -> float:
fn acos(value: float) -> float:
```

## std.net

Synchronous deadline-bound TCP connections/listeners and half-close.

[Behavior and limits](networking.md#stdnet) · [Source](../stdlib/net.wd) · [Regression source](../tests/networking.cmake)

```text
fn connect(host: str, number: int, timeout_ms: int) -> stream:
fn listen(address: str, number: int, timeout_ms: int) -> stream:
fn accept(listener: stream) -> stream:
fn port(socket: stream) -> int:
fn set_timeout(socket: stream, timeout_ms: int):
fn shutdown_write(socket: stream):
```

## std.num

Fixed-shape typed numeric arrays, views, conversions and kernels.

[Behavior and limits](native-computing.md) · [Source](../stdlib/num.wd) · [Regression source](../tests/native_computing.wd)

```text
fn full[S,T](dimensions: S, value: T) -> array[T]:
fn zeros[S](dimensions: S) -> array[float]:
fn ones[S](dimensions: S) -> array[float]:
fn ints[S](dimensions: S, value: int = 0) -> array[int]:
fn from_list[T](values: list[T]) -> array[T]:
fn floats[T](values: array[T]) -> array[float]:
fn integers[T](values: array[T]) -> array[int]:
fn to_list[T](values: array[T]) -> list[T]:
fn shape[T](values: array[T]) -> list[int]:
fn rank[T](values: array[T]) -> int:
fn contiguous[T](values: array[T]) -> bool:
fn copy[T](values: array[T]) -> array[T]:
fn reshape[T,S](values: array[T], dimensions: S) -> array[T]:
fn flatten[T](values: array[T]) -> array[T]:
fn transpose[T](values: array[T], axes: list[int] = []) -> array[T]:
fn slice[T](values: array[T], axis: int, start: int, stop: int, step: int = 1) -> array[T]:
fn sum[T](values: array[T]) -> T:
fn dot[T](left: array[T], right: array[T]) -> T:
fn matmul(left: array[float], right: array[float]) -> array[float]:
fn sqrt(value: float) -> float:
fn sin(value: float) -> float:
fn cos(value: float) -> float:
fn finite(value: float) -> bool:
fn abs(value: float) -> float:
fn close(left: float, right: float, relative: float = 0.000000001, absolute: float = 0.000000000001) -> bool:
```

## std.path

Host path assembly, canonical identities and link-aware containment.

[Behavior and limits](stdlib.md#native-filesystem-and-paths) · [Source](../stdlib/path.wd) · [Regression source](../tests/native_services.wd)

```text
fn combine(base: str, child: str) -> str:
fn name(path: str) -> str:
fn parent(path: str) -> str:
fn absolute(path: str) -> str:
fn normalize(path: str) -> str:
fn resolve(path: str, allow_missing: bool = false) -> str:
fn identity(path: str) -> str:
fn within(path: str, root: str) -> bool:
fn is_absolute(path: str) -> bool:
```

## std.python

EXPERIMENTAL LLVM-only CPython 3.14 interoperability; explicit managed objects and conversions.

[Behavior and limits](python-distribution.md) · [Source](../stdlib/python.wd) · [Regression source](../tests/native_python.wd)

```text
fn import_module(name: str) -> py_object:
fn attr(object: py_object, name: str) -> py_object:
fn call(object: py_object, arguments: list[py_object] = [], keywords: dict[str,py_object] = {}) -> py_object:
fn value[T](object: T) -> py_object:
fn to_json(object: py_object) -> json:
fn tuple(objects: list[py_object]) -> py_object:
fn bytes(object: str) -> py_object:
fn to_bytes(object: py_object) -> str:
fn to_int(object: py_object) -> int:
fn to_float(object: py_object) -> float:
fn to_bool(object: py_object) -> bool:
fn text(object: py_object) -> str:
fn close(object: py_object):
fn closed(object: py_object) -> bool:
fn shutdown():
```

## std.random

OS-backed cryptographic bytes and unbiased bounded integers; no insecure fallback.

[Behavior and limits](stdlib.md#secure-randomness) · [Source](../stdlib/random.wd) · [Regression source](../tests/random.wd)

```text
fn bytes(size: int) -> str:
fn below(bound: int) -> int:
```

## std.sqlite

Prepared statements, bounded JSON rows and rollback scopes; optional SQLite.

[Behavior and limits](backend.md#sqlite) · [Source](../stdlib/sqlite.wd) · [Regression source](../tests/sqlite.wd)

```text
fn open(path: str, timeout_ms: int) -> database:
fn close(connection: database):
fn closed(connection: database) -> bool:
fn version() -> str:
fn prepare(connection: database, sql: str) -> statement:
fn begin(connection: database) -> transaction:
fn commit(scope: transaction):
fn rollback(scope: transaction):
fn changes(connection: database) -> int:
fn last_id(connection: database) -> int:
fn execute(connection: database, sql: str, parameters: list[json]) -> int:
fn query(connection: database, sql: str, parameters: list[json], row_limit: int, byte_limit: int) -> list[dict[str, json]]:
```

## std.stats

Checked integer totals and binary64 online/scaled statistics.

[Behavior and limits](stdlib.md#embedded-modules) · [Source](../stdlib/stats.wd) · [Regression source](../tests/statistics_edges.wd)

```text
fn sum_int(values: list[int]) -> int:
fn mean(values: list[float]) -> float:
fn variance(values: list[float]) -> float:
```

## std.task

Joinable threads, immutable JSON messages and explicit socket handoff.

[Behavior and limits](backend.md#native-tasks) · [Source](../stdlib/task.wd) · [Regression source](../tests/tasks.wd)

```text
fn spawn(work: fn(json) -> json, payload: json) -> task:
fn spawn_io(work: fn(stream, json) -> json, connection: stream, payload: json) -> task:
fn wait(work: task, timeout_ms: int) -> json:
fn done(work: task) -> bool:
fn cancel(work: task):
fn cancelled() -> bool:
fn checkpoint():
fn close(work: task):
```

## std.time

Blocking sleep and monotonic elapsed-time counters.

[Behavior and limits](stdlib.md#native-time) · [Source](../stdlib/time.wd) · [Regression source](../tests/time.wd)

```text
fn sleep(seconds: float):
fn monotonic() -> float:
fn monotonic_ns() -> int:
fn perf_counter() -> float:
fn perf_counter_ns() -> int:
```

## std.tls

Verified TLS clients, server contexts and stream handoff; optional OpenSSL.

[Behavior and limits](backend.md#tls-and-https) · [Source](../stdlib/tls.wd) · [Regression source](../tests/backend.cmake)

```text
fn connect(host: str, port: int, ca_file: str, timeout_ms: int) -> stream:
fn server(certificate_chain_file: str, private_key_file: str) -> tls_context:
fn accept(connection: stream, context: tls_context) -> stream:
fn close(context: tls_context):
fn version() -> str:
```

## std.utf8

Strict Unicode scalar decoding/encoding over byte strings.

[Behavior and limits](stdlib.md#bytes-and-utf-8) · [Source](../stdlib/utf8.wd) · [Regression source](../tests/utf8.wd)

```text
fn decode(text: str) -> list[int]:
fn encode(points: list[int]) -> str:
fn is_valid(text: str) -> bool:
fn length(text: str) -> int:
fn characters(text: str) -> list[str]:
fn slice_text(text: str, start: int, stop: int) -> str:
```

## Builtin index

These names come from the compiler registry. Prefer standard-module wrappers for
OS/numerical services. Signatures/overloads and errors are defined by the linked
contracts; registry presence alone does not establish support in another backend.

| Builtin | Contract |
| --- | --- |
| `array_full` | [Contract](native-computing.md) |
| `array_from_list` | [Contract](native-computing.md) |
| `array_shape` | [Contract](native-computing.md) |
| `array_rank` | [Contract](native-computing.md) |
| `array_contiguous` | [Contract](native-computing.md) |
| `array_copy` | [Contract](native-computing.md) |
| `array_to_list` | [Contract](native-computing.md) |
| `array_reshape` | [Contract](native-computing.md) |
| `array_transpose` | [Contract](native-computing.md) |
| `array_slice` | [Contract](native-computing.md) |
| `array_sum` | [Contract](native-computing.md) |
| `array_dot` | [Contract](native-computing.md) |
| `array_matmul` | [Contract](native-computing.md) |
| `array_affine` | [Contract](native-computing.md) |
| `array_floats` | [Contract](native-computing.md) |
| `array_integers` | [Contract](native-computing.md) |
| `math_sqrt` | [Contract](native-computing.md) |
| `math_sin` | [Contract](native-computing.md) |
| `math_cos` | [Contract](native-computing.md) |
| `math_finite` | [Contract](native-computing.md) |
| `print` | [Contract](stdlib.md) |
| `eprint` | [Contract](stdlib.md) |
| `exit` | [Contract](stdlib.md) |
| `error` | [Contract](errors.md) |
| `len` | [Contract](stdlib.md) |
| `str` | [Contract](stdlib.md) |
| `int` | [Contract](stdlib.md) |
| `float` | [Contract](stdlib.md) |
| `assert` | [Contract](stdlib.md) |
| `append` | [Contract](stdlib.md) |
| `pop` | [Contract](stdlib.md) |
| `range` | [Contract](closures-iterators.md) |
| `iter` | [Contract](closures-iterators.md) |
| `next` | [Contract](closures-iterators.md) |
| `yielded` | [Contract](closures-iterators.md) |
| `finished` | [Contract](closures-iterators.md) |
| `has_item` | [Contract](closures-iterators.md) |
| `step_value` | [Contract](closures-iterators.md) |
| `unwrap` | [Contract](stdlib.md) |
| `keys` | [Contract](stdlib.md) |
| `values` | [Contract](stdlib.md) |
| `get` | [Contract](stdlib.md) |
| `remove` | [Contract](stdlib.md) |
| `slice` | [Contract](stdlib.md) |
| `byte_value` | [Contract](stdlib.md) |
| `byte_string` | [Contract](stdlib.md) |
| `char_count` | [Contract](stdlib.md#bytes-and-utf-8) |
| `chars` | [Contract](stdlib.md#bytes-and-utf-8) |
| `char_slice` | [Contract](stdlib.md#bytes-and-utf-8) |
| `json` | [Contract](data-model.md#typed-json-boundaries) |
| `json_parse` | [Contract](data-model.md#typed-json-boundaries) |
| `json_dump` | [Contract](data-model.md#typed-json-boundaries) |
| `json_null` | [Contract](data-model.md#typed-json-boundaries) |
| `json_kind` | [Contract](data-model.md#typed-json-boundaries) |
| `json_int` | [Contract](data-model.md#typed-json-boundaries) |
| `json_float` | [Contract](data-model.md#typed-json-boundaries) |
| `json_bool` | [Contract](data-model.md#typed-json-boundaries) |
| `json_str` | [Contract](data-model.md#typed-json-boundaries) |
| `json_list` | [Contract](data-model.md#typed-json-boundaries) |
| `json_dict` | [Contract](data-model.md#typed-json-boundaries) |
| `split` | [Contract](stdlib.md) |
| `join` | [Contract](stdlib.md) |
| `strip` | [Contract](stdlib.md) |
| `find` | [Contract](stdlib.md) |
| `replace` | [Contract](stdlib.md) |
| `starts_with` | [Contract](stdlib.md) |
| `ends_with` | [Contract](stdlib.md) |
| `read_text` | [Contract](stdlib.md) |
| `write_text` | [Contract](stdlib.md) |
| `path_exists` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `fs_kind` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `fs_list` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `fs_size` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `fs_mkdirs` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `fs_remove` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_join` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_name` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_parent` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_absolute` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_normalize` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_resolve` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_identity` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_within` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `path_is_absolute` | [Contract](stdlib.md#native-filesystem-and-paths) |
| `time_sleep` | [Contract](stdlib.md#native-time) |
| `env_get` | [Contract](stdlib.md) |
| `random_bytes` | [Contract](stdlib.md) |
| `time_monotonic` | [Contract](stdlib.md#native-time) |
| `time_monotonic_ns` | [Contract](stdlib.md#native-time) |
| `stream_open` | [Contract](networking.md) |
| `stream_read` | [Contract](networking.md) |
| `stream_read_line` | [Contract](networking.md) |
| `stream_write` | [Contract](networking.md) |
| `stream_flush` | [Contract](networking.md) |
| `stream_close` | [Contract](networking.md) |
| `stream_closed` | [Contract](networking.md) |
| `stream_seek` | [Contract](networking.md) |
| `stream_tell` | [Contract](networking.md) |
| `stream_timeout` | [Contract](networking.md) |
| `tcp_connect` | [Contract](networking.md) |
| `tcp_listen` | [Contract](networking.md) |
| `tcp_accept` | [Contract](networking.md) |
| `tcp_port` | [Contract](networking.md) |
| `tcp_shutdown_write` | [Contract](networking.md) |
| `db_open` | [Contract](backend.md#sqlite) |
| `db_close` | [Contract](backend.md#sqlite) |
| `db_closed` | [Contract](backend.md#sqlite) |
| `db_changes` | [Contract](backend.md#sqlite) |
| `db_last_id` | [Contract](backend.md#sqlite) |
| `db_prepare` | [Contract](backend.md#sqlite) |
| `db_begin` | [Contract](backend.md#sqlite) |
| `db_commit` | [Contract](backend.md#sqlite) |
| `db_rollback` | [Contract](backend.md#sqlite) |
| `db_version` | [Contract](backend.md#sqlite) |
| `stmt_close` | [Contract](backend.md#sqlite) |
| `stmt_step` | [Contract](backend.md#sqlite) |
| `stmt_reset` | [Contract](backend.md#sqlite) |
| `stmt_bind_int` | [Contract](backend.md#sqlite) |
| `stmt_bind_float` | [Contract](backend.md#sqlite) |
| `stmt_bind_text` | [Contract](backend.md#sqlite) |
| `stmt_bind_blob` | [Contract](backend.md#sqlite) |
| `stmt_bind_null` | [Contract](backend.md#sqlite) |
| `stmt_columns` | [Contract](backend.md#sqlite) |
| `stmt_name` | [Contract](backend.md#sqlite) |
| `stmt_kind` | [Contract](backend.md#sqlite) |
| `stmt_int` | [Contract](backend.md#sqlite) |
| `stmt_float` | [Contract](backend.md#sqlite) |
| `stmt_text` | [Contract](backend.md#sqlite) |
| `stmt_blob` | [Contract](backend.md#sqlite) |
| `task_spawn` | [Contract](backend.md#native-tasks) |
| `task_spawn_io` | [Contract](backend.md#native-tasks) |
| `task_wait` | [Contract](backend.md#native-tasks) |
| `task_done` | [Contract](backend.md#native-tasks) |
| `task_cancel` | [Contract](backend.md#native-tasks) |
| `task_close` | [Contract](backend.md#native-tasks) |
| `task_cancelled` | [Contract](backend.md#native-tasks) |
| `task_checkpoint` | [Contract](backend.md#native-tasks) |
| `tls_context_new` | [Contract](backend.md#tls-and-https) |
| `tls_context_close` | [Contract](backend.md#tls-and-https) |
| `tls_accept` | [Contract](backend.md#tls-and-https) |
| `tls_connect` | [Contract](backend.md#tls-and-https) |
| `tls_version` | [Contract](backend.md#tls-and-https) |
| `args` | [Contract](stdlib.md) |
| `read_line` | [Contract](stdlib.md) |
| `eof` | [Contract](stdlib.md) |
