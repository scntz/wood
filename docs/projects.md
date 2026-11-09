# Modules, projects, and local packages

```sh
wood init hello
cd hello
wood run -- world
wood test --report=json
wood lock
wood build --locked
wood inspect --locked
```

`wood init hello` creates a runnable application, a library module, and a real
native test in an absent or empty directory. `wood init` or `wood init .` initializes
the empty current directory without deleting/replacing that directory. Existing
files, including hidden files or `.git`, cause refusal; there is no merge/force mode.
The parent directory must already exist. A target that is a filesystem link,
including a Windows junction, is rejected.

The generated project contains exactly:

```text
wood.json                 # format 1, version 0.1.0, no dependencies
src/main.wd               # runnable entry: prints hello, wood (or an argument)
src/greeting.wd           # reusable fn greet(name: str) -> str
tests/test_greeting.wd    # one native assertion test
```

The existing naming convention is retained: the directory basename supplies the
manifest name, non-ASCII-identifier bytes become `_`, and an invalid identifier
(for example `123app`) or reserved `std` falls back to `wood_app`. Thus `my-app`
becomes `my_app`. Trailing separators do not change the derived name. Package
names in manually written manifests still require ASCII identifiers other than
`std`; initialization does not change that rule.

Initialization requires no C++ toolchain or Python. Success prints the project
location and the existing `wood run`, `wood build`, `wood test` next steps. Filesystem
or invalid-argument errors return 2 and honor `--diagnostics=json` (`D5001`). A
quoted empty argument is rejected; use `.` explicitly instead.

New directories are published from staging. Existing empty directories receive
non-overwriting copies, with `wood.json` copied last. An I/O failure during those
copies may leave partial **new** scaffold files; the initializer does not delete
the directory or remove user data to roll back. It does not provide crash durability
or a transaction against concurrent directory changes.

`run`, `check`, `build`, `emit-cpp`, `inspect`, and `test` accept a project directory or
`wood.json`. From a project/subdirectory, omit the input to discover the nearest
ancestor manifest. Explicit `.wd` files retain script behavior and use an enclosing
project's roots/dependencies when one exists.

## Imports

```wood
import std.math as math
from geometry.area import rectangle as area

print(math.gcd(48, 18), area(3, 4))
```

For this example, create `geometry/area.wd` beside the script (or beneath the
project source root) containing `fn rectangle(width: int, height: int) -> int:`
and an indented `return width * height`. The documentation test creates this
helper explicitly. Use `from .helpers import normalize` only when an actual
sibling `helpers.wd` exports that function.

Modules are `.wd` files. Dots map to directories: `geometry.area` resolves to
`geometry/area.wd` beneath the current package's source root. A relative import's
first dot selects the importing file's directory; each additional dot selects
one parent. Resolved files must stay inside that package's declared source root
(or the root project's declared test root). Source symlinks escaping those roots
are rejected, including Windows junctions. Unknown Windows reparse types are
rejected if they cannot be resolved safely. No environment search path or
system-wide module search is used.

`import geometry.area` binds the namespace `geometry.area`; call
`geometry.area.rectangle(...)`. `as` provides a short namespace alias. A `from`
import binds the named function, with an optional alias. Duplicate/reserved bindings
are errors. Module names are not first-class values. Exported functions can be
used as typed native function values; module lookup is resolved statically.

An imported module may contain only imports, record declarations and function definitions. Importing
does not run initialization statements. Functions and records without a leading underscore
are exported; underscore functions are private. Imported bindings are not
automatically re-exported. Cycles are rejected with an import chain. Each resolved
file is loaded once per package context, all reachable functions are type checked,
and shared dependencies receive one set of function symbols in that context.

Library functions still receive explicit parameters and cannot capture script
variables. Runtime errors and frontend diagnostics retain the originating module's
file and source position. Embedded modules use names such as `<std.math>`.

## Manifest format 1

```json
{
  "format": 1,
  "name": "report_app",
  "version": "0.1.0",
  "source": "src",
  "entry": "main.wd",
  "tests": "tests",
  "dependencies": {
    "validation": {"path": "../validation", "version": "1.0.0"}
  }
}
```

Required fields are `format`, `name`, `version`, and `source`. `entry` and `tests`
are optional for libraries. `source` and `tests` resolve relative to the manifest;
`entry` resolves relative to `source`. These paths must remain within their package
and contain no NUL. Source/test directories must exist. Package names and dependency
aliases are ASCII identifiers; `std` is reserved. Versions use exactly three decimal
components, without leading zeros, prerelease markers, or ranges.

Dependencies are **existing local directories** containing their own `wood.json`.
Paths are relative to the declaring manifest and may use `..` to reference another
package. Each specification requires `path` and an exact `version`. Dependencies
can declare their own dependencies. Cycles, mismatched versions, missing manifests,
unknown fields, wrong JSON types, duplicate keys, and malformed UTF-8 are errors.

For `from validation.numbers import measurements`, the first component selects
the current package's declared dependency alias, then `numbers.wd` resolves inside
that dependency's source directory. Without a matching dependency alias, lookup
uses the current package's own source root. Different packages can use the same
alias for different dependencies. Dependencies are not implicitly visible to their
dependents' callers. `std.*` always selects embedded standard-library modules.

Compiling is offline. Fetching belongs to [`wood add`](packages.md), and there is no version-range
solver or installation hook. No dependency code executes during manifest loading, locking, checking, or inspection.
A dependency may also carry an optional string `source`, recorded by
[`wood add`](packages.md). Compilers only require that it is a string, and resolve the
dependency through `path` as usual.
For an author/consumer workflow, including reviewed immutable Git source vendoring
outside the compiler CLI, see [creating libraries](libraries.md).

## Locks and reproducibility

`wood lock` writes `wood.lock` atomically. It records the compiler version string,
embedded standard-library source hashes, exact package versions and relative roots,
dependency edges, manifest-byte SHA-256 hashes, and every `.wd` source hash in each
transitive dependency. The application's own source is editable and is not content
locked; its manifest is locked. Dependency snapshots reject symlinks and Windows
reparse points (including junctions), including
unused source links. Adding/removing unused dependency `.wd` files changes the lock.

When a lock exists, project build/run/check/test/inspect verify it. `--locked`
additionally requires a lock to exist. A mismatch fails with instructions to run
`wood lock` and review the difference; no command silently refreshes it. `wood deps`
prints the currently resolved inventory as JSON for review; `wood deps --locked`
also validates the lock. `lock --locked` is rejected because refreshing and requiring
an unchanged lock are conflicting operations.

After inventory verification, the actual dependency text loaded for compilation
is checked against the verified hashes again. This prevents a source change between
inventory and compilation from silently selecting different dependency bytes.
Relative roots allow a whole project/dependency tree to be relocated intact.

This establishes content-checked local dependency resolution, not byte-for-byte
native binary reproducibility. It does not pin the host compiler/linker binary,
system headers/libraries, environment, CPU, or build flags. Hashes do not authenticate
authors, establish package trust, or sandbox code. A trusted lock must come from a
trusted source. The SHA-256 implementation follows [FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf)
and is tested with known answers and independently against Python `hashlib`; no
FIPS implementation certification is claimed.

## Native tests

`wood test` discovers `.wd` files whose basename begins `test_` under the declared
tests directory, sorts them, and builds/runs each as an independent native program.
Tests import application modules and use normal assertions or application logic.
Any nonzero exit or compiler failure fails a test, and later tests still run.
No discovered tests is an error. `wood test file.wd` runs one script as a test.

`--report=json` emits schema version 1 on stdout: `passed`, `failed`, and a `tests`
array with `file`, `passed`, `exit_code`, and captured combined `output` per test.
Test output cannot corrupt the report's JSON. Compiler diagnostics remain on
stderr; `--diagnostics=json` structures them too. Exit is 0 if all tests pass,
1 for test failures, and 2 for invalid project/driver configuration.

Tests use the same native compiler/options as applications. `--sanitize=address`
instruments generated programs; use it on `run`, `build`, or `test`. It requires
a supporting host compiler and its ASan runtime. It is a diagnostic configuration,
not a deployment recommendation. Native test binaries are currently rebuilt each
run; no build cache is implemented.

## Inspection for humans and agents

`wood inspect` checks the entire reachable program and returns schema version 1
JSON containing `entry` and `modules`. Each module records `file`, `sha256`, resolved
`imports`, and public `exports` with `name`, parameter names/types, return type, and
declaration line. Record exports additionally have `kind: "record"` and `fields`
with field names/types (rather than function parameters/return). It invokes no native compiler and executes no wood code.

Use `wood --language-info` for supported syntax/capabilities, `inspect` for project
interfaces, `check --diagnostics=json` for source errors, and `test --report=json`
for native validation. These are local, deterministic tool interfaces; no model
service or remote agent protocol is bundled.

See [the complete multi-package report example](../examples/project/wood.json) and
[its test](../examples/project/tests/test_report.wd). The project limits are 128
packages, 1 MiB per manifest, 16 MiB per lock/source file, 1,024 reachable modules,
128 import levels, 128 MiB of reachable source, and 4,096 `.wd` files per dependency
snapshot. Manifest JSON uses finite binary64 floats and signed 64-bit integers;
the current schema requires only integer format numbers and string versions.

## Nominal type identities in inspection

Display strings (`type` and `returns`) remain compatible with earlier tools.
Parameters/record fields also have `type_ref`; functions have `returns_ref`;
record exports have `type_ref`. A scalar descriptor is `{"kind":"int"}` (or
another scalar name). A list descriptor has `kind: "list"` and `element`; a
mapping has `kind: "dict"`, `key` and `value`. A record descriptor has
`kind: "record"`, `module` (the declaring module's reported file) and `name`.
The module/name pair distinguishes nominal types, including nested containers
and aliases. Record descriptors refer to declarations rather than recursively
expanding fields. IDs are meaningful within an inspection report; absolute module
paths can change on relocation. These are additive schema-version-1 fields.

A tuple descriptor has `kind: "tuple"` and an `elements` array. An optional has
`kind: "optional"` and `element`; None is `{"kind":"None"}`. A generic parameter
has `kind: "type_parameter"` and its scoped `name`. Function exports add a
`type_parameters` name array; parameter entries add `mode` (`positional_only`,
`positional_or_keyword`, `keyword_only`) and `has_default`. Default source text is
not executed or embedded in inspection output. These are additive schema 1 fields.

A function descriptor has `kind: "function"`, a `parameters` array of type
descriptors, and `returns` containing the result descriptor. These preserve nominal
identities inside callback signatures. Function exports add an `external` boolean
identifying scalar C ABI declarations. No host compiler/library is invoked during
inspection; an external signature does not verify a foreign library's ABI.

Test entries additionally include `driver_error`: an empty string normally,
`D5003` for a timeout, `D5004` for a capture limit or `D5005` for cancellation.
A failed test retains bounded output and an explanatory driver message, and later
tests continue. See [process limits](cli.md#process-limits).

Generic nominal record descriptors also include an `arguments` array of structural
type descriptors. Record exports include `type_parameters` and `methods`. Methods
reuse function descriptors and identify their `receiver` as `value` or `mutable`,
with positional-only `self` included in `parameters`; free functions use receiver
`none`. Declaration identities always refer to the original defining module, even
when concrete instances were created through imported aliases. See
[generic-records.md](generic-records.md) for the native call contract.
