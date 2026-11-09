# Troubleshooting wood

Start with `wood --version` and `wood --help`. Use a fresh output directory and
keep your source, existing binaries and dependency lock intact while diagnosing.
The [support matrix](support.md) identifies the backend/platform you are using.

## Source errors

Run `wood check app.wd --diagnostics=json`. It checks imports and types without
running source or invoking C++. Diagnostics are JSON Lines on stderr, not stdout;
each contains `severity`, `code`, `file`, `line`, `column`, `message`. Positions
are one-based bytes, or zero when unknown. Only the first frontend error is
reported; fix it and check again. Parse codes and fields, not English wording.

This deliberately invalid program produces E3002 at the assigned string:

```wood
count = 1
count = "two"
```

Keep the value's type consistent, or introduce a separate variable:

```wood
count = 1
label = "two"
assert(count == 1 and label == "two")
```

| Symptom | Action |
| --- | --- |
| E3007/E3005 unknown variable/function | Check spelling, block scope and explicit imports. Suggestions are conservative; a missing suggestion proves nothing. |
| E3002 type mismatch | Compare actual/expected types at the reported expression. Locals retain their initial type; collections are invariant. |
| E3003 call arguments | Use the actual parameter names/order. Builtins and function pointers take positional arguments; omitted literal defaults apply only where documented. |
| E3014 empty collection | Add `items: list[int] = []` or another concrete contextual type. |
| E3013/E3030 returns | Every value-returning path needs return/raise/direct exit. Annotate recursive inference cycles; annotations do not repair incompatible returned values. |
| E3101/E3104 imports | Check the package's source root, alias and exported name. Underscore names are private and imported names are not re-exported. |
| E3102 import cycle | Pass values/functions explicitly and remove the cycle; modules do not run initialization code. |

`wood explain E3030` works offline, and `wood explain --help` lists every code.
See [all diagnostic categories](cli.md#diagnostics-and-exit-codes).

## Native compilation

`cannot run 'opt'` on Linux means the LLVM 19 tools are not on your `PATH`. On Debian 13, run
`sudo apt install llvm-19 binutils` and `export PATH="/usr/lib/llvm-19/bin:$PATH"`, and add that line to your shell profile. On
Windows the tools ship with wood, so the same message means the installation is damaged. Reinstall it.

D5001 identifies driver/filesystem/process-start problems; D5002 means the native
compiler returned failure. Read the captured tool output. Verify that `--cxx`
or `WOOD_CXX` names **one compiler executable**, with no embedded flags.
On Windows open an x64 Visual Studio developer shell; locating `cl.exe` alone
does not configure `INCLUDE` and `LIB`. On Linux use the exercised GCC 14 or
Clang 19/libstdc++ family, or validate another toolchain yourself.

Try `wood emit-cpp app.wd -o app.cpp` to distinguish frontend/emission from native
compilation. Preserve the generated file for a bug report; it can contain source
paths and embedded application literals, so review it for private information.
`--sanitize=address` needs the selected toolchain's ASan runtime; a missing runtime
is an environment error, not a program test pass.

## Project and lock failures

`wood init` requires a new or empty, non-linked target; even `.git` makes a
directory populated. Initialize an empty child directory when needed. There is
no overwrite/merge/force mode.

Inspect `wood.json`, run `wood deps --locked` and `wood inspect --locked` from the
project. A stale lock can mean intentionally edited source or an unexpected
dependency change. Review the diff and dependency bytes **before** `wood lock`;
never refresh automatically to hide verification failure. Only local path
dependencies with exact versions are supported. Native archives linked with
`--link-library` are trusted and are not hashed by `wood.lock`.

`wood test --report=json` emits a single report on stdout and diagnostics on
stderr. Empty discovery is an error; tests must be `test_*.wd` under the declared
test root. Nonzero child exits fail tests. D5003/D5004/D5005 distinguish deadline,
capture limit and handled cancellation. An unfinished test is not a pass.

## Python packages

`wood add python@...` runs a CPython that wood downloaded into its cache, so a per-application
firewall sees a new program. If it is blocked, wood reports **D5102** with a network failure, and
on Windows the details include `WinError 10013`. Allow that Python, which lives under
`WOOD_CACHE_DIR/python/`, or add the package from a machine that can reach PyPI and commit
`wood.lock`. Note that wood's cache location changes the path a firewall has to allow.

## Runtime and data failures

Uncaught checked errors exit 70. `try`/`except err` catches wood errors, not OS
signals, native memory corruption or arbitrary allocation/library failure.
Use `err.code`, `err.file`, `err.line`, `err.column`; rethrow with `raise err` or a bare `raise`.
Use [resource scopes](errors.md) for cleanup, not Python exception syntax.

- Strings are bytes: `len("á") == 2`. Use `std.utf8` for scalar operations.
- `int`/`float` string parsing is strict; call `strip` explicitly when intended.
- CSV needs a one-byte delimiter; whole-document APIs retain input/output memory.
- Network reads can be short; use `std.io.read_exact` with a bounded count.
- R4020/R4021 can indicate absent/incompatible SQLite/OpenSSL libraries.
  Follow [trusted loader configuration](backend.md#native-dependencies); never
  disable TLS peer verification to repair a trust failure.
- OBJ importing intentionally rejects unsupported records and non-triangles.
  Consult [accepted format](obj.md#accepted-format), not a general OBJ parser's behavior.

`wood run` recompiles each time. Use `wood build` and run its executable for
repeated execution. Compile time isn't run time: to measure speed, follow [measuring well](performance.md#measuring-well).

## Report a reproducible problem

Include the smallest input, exact command and exit status, wood/native compiler
versions, backend, OS/architecture and expected/actual output. Remove secrets,
personal paths and proprietary data. Preserve failures and partial checks honestly.
For suspected vulnerabilities use [SECURITY.md](../SECURITY.md), not a public issue
with live credentials or private input.
