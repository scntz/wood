# CLAUDE.md

[AGENTS.md](AGENTS.md) holds the project rules. This file repeats them for Claude Code and adds practical notes. If the two
disagree, follow AGENTS.md and fix this file. Never delete or rename AGENTS.md.

## Read first

- Writing wood programs or changing the language: [docs/agents.md](docs/agents.md), [docs/language.md](docs/language.md).
- Compiler, runtime or library work: [docs/architecture.md](docs/architecture.md), [docs/contributors.md](docs/contributors.md).
- Arrays, views, borrowing, numerical kernels or numeric-buffer FFI: [docs/native-computing.md](docs/native-computing.md).
- What is supported: [docs/support.md](docs/support.md). Packages: [docs/packages.md](docs/packages.md),
  [docs/projects.md](docs/projects.md).

## Rules in short

- wood 1.1.0, language 0.4.0, native runtime ABI 4. wood is no longer maintained: this repository is archived, and forks
  carry it on.
- Keep wood's own syntax (`fn`, indentation and `:`). No new syntax without the owner's agreement.
- wood never needs Python. Python interop stays optional and experimental. No third-party libraries.
- Keep the lexer, parser, semantic, backend and driver layers separate. Public changes update `docs/language.md`,
  `docs/grammar.ebnf`, `docs/language.json` and other metadata.
- Keep value semantics, checked integers, checked indexing and the documented floating-point behavior.
- Match the surrounding code style and comment density.
- Compiler changes must still bootstrap with stage 2 and stage 3 identical. See [docs/native-backend.md](docs/native-backend.md).

## Layout

`src/` and `include/` hold the C++23 reference compiler, the default `wood` in a source build. `bootstrap/src/` holds the
compiler written in wood, which releases ship: its LLVM backend links against a C17 runtime object in `runtime/native/`
and never falls back to C++ (B3501). Package commands are in `bootstrap/src/package_manager.wd` and `package_tools.wd`.
`stdlib/` holds the 22 standard modules and experimental `std.python`. `packages/` holds the official packages and the
installer engine, `tools/installer/` the Windows wizard and Linux installer, `catalog/` the package catalog,
`tools/playground/` the browser playground runtime and `tools/launch/` the owner's launch notes.

## Build and test

```sh
cmake -S . -B build -A x64 -DWOOD_PYTHON_TESTS=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure -R <suite>
```

On Linux use `-G Ninja -DCMAKE_BUILD_TYPE=Release` and plain `ctest`. MSVC native tests need a Visual Studio x64 developer
shell. `ctest --test-dir build -N` lists the suites, and [docs/contributors.md](docs/contributors.md#build-and-test) says
which ones a change needs. A skipped, interrupted or timed-out check is not a pass, and a failure is reported along with its
rerun.

## Git

- Work goes to `main`. Commit and push finished, tested work with a normal fast-forward push; this is standing permission.
  Use the configured Git identity and add no AI or co-author lines.
- The workflows run on every push to `main` and on pull requests.
- Never force-push or rewrite history. Tags, releases, visibility and the Marketplace need the owner's explicit
  approval. Published releases are never replaced.

## Practical notes

- Machines differ: check which shell you are in before assuming path or quoting rules, and run long Linux scripts from a
  file rather than an inline `sh -c` string.
- `build*/`, `install/` and `out/` hold large ignored output. Don't grep the whole checkout; search `src`, `include`,
  `runtime`, `stdlib`, `bootstrap`, `packages`, `tools`, `tests` and `docs`.
- Use new output directories: the installer and package scripts refuse existing ones.
- TLS checks need OpenSSL 3. On Windows the runtime loads `libssl-3-x64.dll` and `libcrypto-3-x64.dll` only from System32
  or an absolute `WOOD_OPENSSL_DIR`; PATH is ignored. A missing library gives R4021, which is an environment problem, not a pass.
- Before long builds or benchmarks, check that no other heavy program is running, and ask if one is.
