# Tests

CTest runs these suites. List them with `ctest --test-dir build -N`, and see
[contributors](../docs/contributors.md#build-and-test) for which to run after a given change.

| Path | Purpose |
| --- | --- |
| `*.cmake` | Suite drivers that run real commands, for example `package_manager`, `diagnostics_quality`, `official_fetch` and `documentation` |
| `*.wd` | wood programs those suites compile and run. `test_*.wd` files are test programs |
| `*.cpp`, `*.hpp` | Unit tests of the C++ reference compiler and runtime |
| `native/` | Native backend, runtime and bootstrap suites |
| `fuzz/` | Mutation fuzzers for the compiler and for parsers of untrusted text |
| `planted_programs.cmake` | A project cannot supply a program in place of a tool, and on Windows a batch file cannot be started |
| `soak.wd` | A long run of the runtime's allocating paths, which fails on a wrong result or growing memory |
| `lsp/` | Language server protocol tests. They need Node.js and a native `wood` |
| `playground/` | Browser runtime compared with native wood. They need Node.js and `tools/playground/build.sh` output |
| `fixtures/` | Test inputs, including throwaway TLS identities and OBJ files. None is a real credential |
| `ecosystem_gui/`, `qualification/` | Multi-file programs used by suites |

Suites that need a native compiler run when you configure with `-DWOOD_NATIVE_FOR_PACKAGE_TESTS=path/to/wood`.
A skipped, interrupted or timed-out check is not a pass. Some test programs use emoji and other non-ASCII text on
purpose, to exercise UTF-8 handling.
