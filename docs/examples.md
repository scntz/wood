# Runnable examples

## Community library walkthrough

The [library author](../examples/library_author/README.md) supplies a public
[record and function](../examples/library_author/src/measurements.wd),
[real tests](../examples/library_author/tests/test_measurements.wd) and an exact
transitive [units module](../examples/library_author/vendor/northstar_units/src/mass.wd).
Follow [the author/consumer guide](libraries.md) to export a reviewed commit and
build an independent, relocatable consumer with the normal CLI.

## Official-package applications

[Media Desk](../examples/media_desk/README.md) is a Windows desktop library for images and
audio, with [entry source](../examples/media_desk/src/main.wd). It combines the GUI, image,
audio, config and log packages with SQLite storage, and
[a real UI test](../tools/test-media-desk.ps1) drives it through its own window handles.

[wood Notes](../examples/desktop_notes/README.md) is a real Windows-native SQLite
desktop application, with [entry source](../examples/desktop_notes/src/main.wd).
It consumes the official CLI and experimental GUI packages and a shared
[notes model](../examples/packages/notes_store/src/store.wd), independently tested
by [its model contracts](../examples/packages/notes_store/tests/test_store.wd).
The [bounded history formatter](../examples/desktop_notes/src/presentation.wd)
has independent [large-history tests](../examples/desktop_notes/tests/test_presentation.wd)
that run without a GUI adapter on Windows/Linux.
The [fresh-database seed helper](../examples/desktop_notes/tests/seed_history.wd)
is only used to test the GUI with a long history.
Follow [official package workflows](official-packages.md) for explicit native
inputs, source locks and platform limits; ordinary std-only programs need none
of the GUI dependencies.

[configured_report.wd](../examples/configured_report.wd) is a bounded UTF-8
CSV-to-JSON CLI. Run `wood examples/configured_report.wd -- input.csv new-report.json`
with `name,quantity` columns; `WOOD_CSV_DELIMITER` optionally selects a single-byte
separator. Repeated names accumulate checked integer totals. Existing output
files are refused. See [configuration and errors](stdlib.md#environment-configuration).

Commands below start in the repository root, or the installed manual/source root
that contains `examples/`. Put outputs in your own scratch directory. Commands
that write files can truncate them unless the application explicitly refuses
existing output. Use an absolute compiler path when working elsewhere.
`wood file.wd -- arguments` forwards arguments; the `--` is required.

Two more applications use the newer official packages.
[Audio player](../examples/audio_player/src/main.wd) mixes tones, writes WAV files and
plays them through the [audio package](../packages/wood_audio/README.md); playback needs
its Windows adapter. [Image tool](../examples/image_tool/src/main.wd) draws, converts,
resizes and filters PNG and BMP files with the pure wood
[image package](../packages/wood_image/README.md).

## Learn and process data

| Source | Command/input | Behavior and validation |
| --- | --- | --- |
| [hello.wd](../examples/hello.wd) | `wood examples/hello.wd` | Two greetings; native `integration` suite |
| [control_flow.wd](../examples/control_flow.wd) | `wood examples/control_flow.wd` | Checked loop/branch assertions; `integration` |
| [fibonacci.wd](../examples/fibonacci.wd) | `wood examples/fibonacci.wd` | Recursive integer function, prints first twelve terms |
| [primes.wd](../examples/primes.wd) | `wood examples/primes.wd` | Trial division with checked arithmetic |
| [filter_stdin.wd](../examples/filter_stdin.wd) | `wood examples/filter_stdin.wd` with redirected text stdin | Strips comments/blank lines; `applications` |
| [native_word_counts.wd](../examples/native_word_counts.wd) | `wood run examples/native_word_counts.wd < input.txt` | Native stdin, monotonic timing, insertion-order counts and removal; LLVM runtime parity gate |
| [parallel_word_counts.wd](../examples/parallel_word_counts.wd) | `wood run examples/parallel_word_counts.wd -- first.txt second.txt` | Bounded concurrent file readers, scoped joins, isolated counts and deterministic JSON reduction; native task gate |
| [sqlite_inventory.wd](../examples/sqlite_inventory.wd) | `wood run examples/sqlite_inventory.wd -- inventory.db stock.csv` | CSV `name,quantity`; prepared inserts, atomic rollback and deterministic JSON; optional SQLite, reference/native differential gate |
| [text_report.wd](../examples/text_report.wd) | `wood examples/text_report.wd -- numbers.txt report.txt` | Signed decimal lines, comments/blank lines; `applications` |
| [sales_report.wd](../examples/sales_report.wd) | `wood examples/sales_report.wd -- sales.csv sales.json` | Header `category,quantity,unit_cents`; checked totals and JSON; `expressions` |
| [typed_catalog.wd](../examples/typed_catalog.wd) | `wood examples/typed_catalog.wd -- examples/typed_catalog_input.json catalog.json` | Generic transforms, validated JSON, optionals; `typed_interfaces` |
| [route_planner.wd](../examples/route_planner.wd) | `wood examples/route_planner.wd -- examples/routes.json routes-output.json` | Queue/heap and weighted directed routes; `generic_records` |
| [unicode_report.wd](../examples/unicode_report.wd) | `wood examples/unicode_report.wd -- input.txt unicode.json` | Strict UTF-8 scalar counts; `native_foundations` |
| [tree_report.wd](../examples/tree_report.wd) | `wood examples/tree_report.wd -- directory all 1000` | JSON inventory on stdout; budgeted traversal skipping links; `native_ecosystem` |
| [delimited_log_report.wd](../examples/delimited_log_report.wd) | `wood examples/delimited_log_report.wd -- events.log` | Pipe-separated level/category/message with explicit limits; `performance` |
| [log_report.wd](../examples/log_report.wd) | `wood examples/log_report.wd -- examples/access_log.jsonl log.json 100 20` | JSON-lines lazy pipeline; `closures_iterators` |
| [timing.wd](../examples/timing.wd) | `wood examples/timing.wd` | Native counters and checksum; `time`; illustrative elapsed reading, not a benchmark |

For `sales.csv`, use:

```text
category,quantity,unit_cents
books,2,500
pens,3,100
```

For `events.log`, use:

```text
INFO|startup|ready
WARN|storage|space low
```

## Geometry and native integration

[python_numpy.wd](../examples/python_numpy.wd) is an optional LLVM-only example:
`wood run examples/python_numpy.wd --backend=llvm`, with a selected CPython 3.14
environment containing NumPy. It creates/reshapes arrays, extracts native results,
multiplies matrices and catches a Python exception. The dedicated `native_python`
test checks its output. See [Python packages](python-distribution.md). This example isn't part of the default example
tests, which never need Python.

| Source | Command/input | Boundaries and validation |
| --- | --- | --- |
| [mesh_import.wd](../examples/mesh_import.wd) | `wood examples/mesh_import.wd -- tests/fixtures/obj/tetrahedron.obj transformed.obj` | Load, inspect, transform, export; `obj_import` and Linux installed gate |
| [mesh_render](../examples/mesh_render/src/main.wd) | `wood run examples/mesh_render -- torus.png` | Renders a mesh to a PNG with `std.geometry` and `wood_image`, in pure wood |
| [mesh_torus.wd](../examples/mesh_torus.wd) | `wood examples/mesh_torus.wd -- torus.obj 64 32` | Arrays, normals, affine transforms; `native_computing` with independent OBJ oracle |
| [native_grid_mesh.wd](../examples/native_grid_mesh.wd) | `wood examples/native_grid_mesh.wd` | Writes integer mesh to stdout; redirect to a new OBJ path; native LLVM fixtures |
| [ffi.wd](../examples/ffi.wd) | `wood examples/ffi.wd --link-library PATH` | Link the built `wood_ffi_fixture` archive; exact scalar/byte/callback C ABI; `native_ecosystem` |
| [stream_copy.wd](../examples/stream_copy.wd) | `wood examples/stream_copy.wd -- input.bin new-output.bin 1048576` | Bounded copy refusing existing destination; `networking` |
| [https_json_server.wd](../examples/https_json_server.wd) | `wood run examples/https_json_server.wd -- 127.0.0.1 8443 chain.pem key.pem 0` | Sequential verified TLS transport, bounded HTTP JSON health/sum endpoints; optional OpenSSL 3, no application authentication |

The [OBJ contract](obj.md) is triangle-only; unknown records, textures and
materials fail explicitly. The grid example produces no normal records; the
native mesh importer/exporter recomputes normals when exporting.

## Network services

| Source | Invocation | Requirements and validation |
| --- | --- | --- |
| [http_get.wd](../examples/http_get.wd) | `wood examples/http_get.wd -- 127.0.0.1 PORT /health` | Running HTTP endpoint; `networking` loopback fixtures |
| [http_json_server.wd](../examples/http_json_server.wd) | `wood examples/http_json_server.wd -- 127.0.0.1 PORT REQUESTS READY_FILE` | Finite sequential loopback service; `networking` |
| [https_client.wd](../examples/https_client.wd) | `wood examples/https_client.wd -- localhost PORT /health CA_FILE` | OpenSSL 3, server and trusted CA; `backend` |
| [backend.wd](../examples/backend.wd) | `wood examples/backend.wd -- app.db CHAIN_FILE KEY_FILE PORT REQUESTS READY_FILE` | SQLite, OpenSSL, finite task-backed HTTPS service; `backend` |

Read [networking](networking.md) and [backend services](backend.md) before running
a server. These are bounded application examples, not production web frameworks.
Repository TLS identities are public test fixtures and must not be deployment
credentials. The documentation checker never contacts public network endpoints.

## Multi-file projects

Run `wood test PROJECT --locked --report=json` first; the included locks verify
the local dependency inventory. Use the manifest entry to run/build the program.

| Project/source | Role |
| --- | --- |
| [project/wood.json](../examples/project/wood.json) | Local dependency workflow; `projects` suite |
| [project/src/main.wd](../examples/project/src/main.wd) | Executable report entry |
| [project/src/report.wd](../examples/project/src/report.wd) | Reusable imported report function |
| [project/tests/test_report.wd](../examples/project/tests/test_report.wd) | Native assertions |
| [catalog/wood.json](../examples/catalog/wood.json) | JSON catalog project; `data_applications` |
| [catalog/src/main.wd](../examples/catalog/src/main.wd) | JSON input/output CLI |
| [catalog/src/report.wd](../examples/catalog/src/report.wd) | Imported record/report logic |
| [catalog/tests/test_report.wd](../examples/catalog/tests/test_report.wd) | Record/JSON tests |
| [archive/wood.json](../examples/archive/wood.json) | Versioned endian-explicit binary format; `native_foundations` |
| [archive/src/main.wd](../examples/archive/src/main.wd) | Archive encode/decode CLI |
| [archive/src/codec.wd](../examples/archive/src/codec.wd) | Reusable checked binary codec |
| [archive/tests/test_codec.wd](../examples/archive/tests/test_codec.wd) | Exact format assertions |
| [packages/catalog_schema/src/models.wd](../examples/packages/catalog_schema/src/models.wd) | Catalog record dependency |
| [packages/rules/src/bounds.wd](../examples/packages/rules/src/bounds.wd) | Transitive rules dependency |
| [packages/validation/src/numbers.wd](../examples/packages/validation/src/numbers.wd) | Explicit package-alias dependency |

The `documentation` test checks that every example is listed here and that the sources and package tests build. The
named suites run them with real inputs, including the inputs they should reject.
