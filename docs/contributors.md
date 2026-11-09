# Working on the compiler

This page is a map of the compiler for people changing it. To write programs in wood, start with [the tutorial](tutorial.md)
instead.

## Where things are

| Area | Source | Read |
| --- | --- | --- |
| C++ frontend | `src/lexer.cpp`, `src/parser.cpp`, `src/semantic.cpp`, `include/wood/compiler.hpp` | [Architecture](architecture.md), [language](language.md), [grammar](grammar.ebnf) |
| C++ code generation and runtime | `src/codegen.cpp`, `runtime/wood_*.hpp` | [Data model](data-model.md), [arrays](native-computing.md), [closures](closures-iterators.md) |
| Driver and projects | `src/main.cpp`, `src/process.cpp`, `src/project.cpp`, `src/modules.cpp` | [CLI](cli.md), [projects](projects.md), [calling C](native-ffi.md) |
| Frontend in wood | `bootstrap/src/lexer.wd`, `parser.wd`, `modules.wd`, `body_semantics.wd`, `checked_program.wd` | [Bootstrap overview](../bootstrap/README.md), [compiler in wood](bootstrap-compiler.md) |
| C++ emitter and driver in wood | `bootstrap/src/emit_cpp.wd`, `driver*.wd`, `src/selfhost_host.cpp` | [`wood-selfhost`](selfhost-driver.md) |
| LLVM backend | `bootstrap/src/emit_llvm.wd`, `native_main.wd`, `runtime/native/` | [LLVM backend](native-backend.md) |

The C++ compiler is the reference, and the default in a source build. The installers ship the compiler written in wood with
the LLVM backend. Keep the emitter, runtime IR and runtime object at the same ABI.

## Build and test

Build as the [README](../README.md#build-from-source) shows, with a separate build directory for each compiler or
configuration. With Visual Studio, use `cmake --build build --config Release` and `ctest --test-dir build -C Release`, from a
developer shell for the native tests. `-DWOOD_PYTHON_TESTS=OFF` turns off the tests that compare against Python; Python is
never needed to build or run wood.

`ctest --test-dir build -N` lists the suites. Run the ones your change affects first, then the wider set:

| Change | Suites |
| --- | --- |
| Frontend and diagnostics | `compiler`, `explain_unit`, `explain`, `diagnostics_quality`, and the expression, type and record suites |
| Language server and VS Code extension | `lsp_protocol` (needs Node), and `editors/vscode/test/run.mjs` with `WOOD` and `CODE` set |
| Runtime allocation | `runtime_heap`, and `tools/bench-dx.ps1` before and after |
| Values and ownership | `runtime`, `arrays_runtime`, `performance_runtime`, `performance`, `closures_iterators` |
| Libraries and examples | `applications`, `data_applications`, `native_computing`, `obj_import`, `native_ecosystem` |
| Projects and processes | `project_unit`, `project_init`, `projects`, `process`, `operational` |
| The compiler in wood | `bootstrap_compiler`, `selfhost_*` |
| Linux | [The Linux checks](linux.md), with separate GCC, Clang and sanitizer builds |
| LLVM | [The native suites](native-backend.md#tests), including the three-stage bootstrap |
| Documentation | `documentation`: links, the API index, and every code block. See the Documentation section of [CONTRIBUTING](../CONTRIBUTING.md) |
| Embedded assets and build portability | `embedded_text`, `embedded_assets` |

A skipped TLS or SQLite check isn't a pass. `WOOD_SANITIZERS=ON` instruments the compiler and test binaries, not the programs
they build; [CONTRIBUTING](../CONTRIBUTING.md) explains how to instrument those. Don't use sanitizer
builds for timing.

`native_stdlib` compares the installed LLVM compiler with the C++ reference at `-O0` and `-O2`. Its extra check of the C++
emitter needs the reference build's `bootstrap-runtime.hpp`; pass `-DCPP_RUNTIME=/absolute/runtime.hpp` if it isn't next to
the reference executable.

## Sending a change

Keep the lexer, parser, semantic, backend and driver separate. Add tests for what should work and what should fail, and keep
error codes and report formats stable. When the language changes, update the reference, the grammar and the metadata together.
Read [CONTRIBUTING](../CONTRIBUTING.md), the [security policy](../SECURITY.md) and the [third-party notices](../THIRD_PARTY_NOTICES.md),
and don't commit dependencies, build output or binaries. Agents follow [AGENTS.md](../AGENTS.md).
