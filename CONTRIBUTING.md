# Contributing to wood

Wood is an open-source research project. The upstream repository has no regular release or review
schedule. Forks are the practical route for continuing the work and maintaining variants. The
build and test guidance below describes the upstream workflow.

Read [the architecture](docs/architecture.md) and [language semantics](docs/language.md) before changing the compiler.
wood is MIT OR Apache-2.0, as [LICENSE](LICENSE) says. Keep the copyright and third-party notices, and identify external
material before adding it. Do not commit secrets, local build output or performance claims nobody can reproduce.

## Build and test

Build a Release configuration and run CTest, using the commands in the [README](README.md#build-from-source). The test
executables use explicit checks that stay active with `NDEBUG` defined, and timeouts bound accidental hangs.
[Contributors](docs/contributors.md#build-and-test) maps each kind of change to the suites it needs.

Record the exact validation commands, platform and compatibility implications with each change.

| Suite | What it checks |
| --- | --- |
| `compiler` | Frontend acceptance, rejection and diagnostics |
| `runtime` | Arithmetic boundaries and range termination |
| `integration` | The actual CLI and native toolchain |
| `applications` | Collections, copy-on-write, text, files, arguments, stdin and error paths |
| `data_applications` | Dictionary, record, JSON and CSV programs at O0 and O2, and a packaged catalog CLI |
| `csv_differential` | 318 CSV parse and write cases against Python's CSV module, when Python is present |
| `support`, `differential` | Strict JSON and SHA-256 vectors, compared with Python's standard library when available |
| `project_unit`, `projects` | Lock inventories, multi-package imports, project workflows and rejection paths |
| `process`, `operational` | Child process deadlines and capture caps, CLI failure aggregation and artifact preservation |
| `official_fetch` | Every official package, installed from a local mirror of this repository at its catalog pin |
| `diagnostics_quality` | The advice in common error messages |
| `lsp_protocol` | `wood lsp` over real pipes |
| `documentation` | Links, anchors, public API coverage, metadata and extracted examples |

`official_fetch`, `diagnostics_quality` and `lsp_protocol` need the native CLI, supplied with
`-DWOOD_NATIVE_FOR_PACKAGE_TESTS=path/to/wood`. `editors/vscode/test/run.mjs` runs the extension inside a real VS Code with a
throwaway profile. Python is optional, and used only as a differential oracle: configure `-DWOOD_PYTHON_TESTS=OFF` for the
complete native-only set. Keep test scratch files inside the build directory.

## Changing the compiler

1. Add an acceptance or rejection case that exercises the affected language rule.
2. For generated behavior, add an end-to-end native assertion or an integration case.
3. Update the reference, agent guide and metadata when public semantics change.
4. Run the relevant tests, then the complete CTest suite before submitting.
5. State which compiler, configuration and operating system you actually used.

Keep lexer, parser, semantic analysis, code generation and process handling separate. Do not bypass semantic checks with
raw C++ fragments. Preserve diagnostic codes, or document a deliberate compatibility change. Generated identifiers must use
symbol IDs, and wood source names must never become native identifiers.

Normal compiler work needs no new dependency. A proposed dependency should solve a concrete problem and state its build,
licensing and cross-platform costs. Discuss major syntax or runtime changes in a design proposal first, with rejected
alternatives and a migration and testing strategy.

## Sanitizers and performance

`-DWOOD_SANITIZERS=ON` enables ASan on MSVC and ASan with UBSan on GCC and Clang, for the compiler and test binaries. It does
not instrument programs the CLI produces. Use `wood test tests/recovery.wd --sanitize=address` for a native ASan run. Its
toolchain runtime must be on the executable's library search path, which a Visual Studio developer shell sets up on Windows.
For UBSan or custom flags, use `emit-cpp` and compile that translation unit yourself.

For performance work, use [the benchmark harness](docs/performance.md), and check its output before accepting timings.
Sanitizer builds are correctness checks, not performance baselines.

## Linux

Use [the Linux gate](docs/linux.md) with separate GCC and Clang build directories for portability changes. It records compiler
and library details, runs the existing regressions and an installed native OBJ workflow, and adds emitted-code ASan and UBSan
checks. Linux must actually run before you report Linux passes. Script syntax checks, static inspection and Windows runs do not
count.

## Documentation

Every ` ```wood ` block in the README and `docs/` runs in the `documentation` test, in order, as registered in
[docs/documentation.json](docs/documentation.json). Adding, removing or reordering a block means updating that file. Keep
examples self-contained, or declare their fixtures there. `docs/stdlib-api.md` is generated from the standard library, so
change the library rather than editing it by hand. Agents changing the compiler follow [AGENTS.md](AGENTS.md); agents
writing applications use [the application guide](docs/agents.md).

## Bugs

This repository takes no bug reports. A useful report for a fork has the smallest reproducing `.wd` file, `wood --version`,
your native compiler version and OS. For security, see [SECURITY.md](SECURITY.md).
