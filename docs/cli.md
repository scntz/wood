# Command-line reference

These commands work the same with the installed `wood` and with a source build. The parts about the C++ toolchain and
`emit-cpp` apply only to a source build: the installed `wood` builds with LLVM, adds `emit-llvm`, and needs no C++ compiler.
See the [native toolchain](native-cli.md) for its extra options.

```text
wood script.wd
wood run script.wd
wood script.wd -- argument1 "two words"
wood check script.wd [--diagnostics=json]
wood build script.wd [-o executable] [-O0|-O2] [--cxx PATH]
wood emit-cpp script.wd [-o script.cpp]
wood --version
wood --language-info
wood --help
wood explain E3030
wood init [directory]
wood lock [project]
wood deps [project] [--locked]
wood inspect [script.wd|project] [--locked]
wood test [script.wd|project] [--locked] [--report=json]
wood add owner@package[#version] | github@owner/repo[#ref] | python@name[#version]
wood install dependency SOURCE
wood install [--locked] [--offline]
wood remove ALIAS | python@name
wood update [ALIAS | python@name]
wood list
wood info owner@package
wood build [project] --bundle-python [-o DIRECTORY]
wood lsp [--stdio]
wood fmt [--check] [file.wd | directory ...]
wood init --lib [directory]
wood package validate|api|pack|prepare|listing|catalog
```

`wood lsp` starts the [language server](editors.md) for editors. Package commands, `fmt` and the language
server belong to the wood-written native CLI; see
[how packages work](packages.md) and [publishing packages](package-publishing.md).

`wood fmt` rewrites `.wd` files in one layout: four spaces per block, one space around binary
operators and after commas and colons, none inside brackets, `f(x=1)` for keyword arguments and
`x: int = 1` for annotated defaults, two spaces before a trailing comment and at most two blank lines
in a row. Comments, line breaks inside brackets and each file's line ending are kept. Directories are
searched for `.wd` files, skipping the directories the language server skips (hidden ones, `build*`,
`node_modules`, `dist`, `install`, `out` and `vendor`); with no path it formats the project around
the current directory. `--check` changes nothing, lists the files that would change and exits 1 if
there are any. A file that does not parse is reported and left alone. The formatter checks that the
result lexes to exactly the same tokens, so it cannot change what a program does.

`run` is the default. It checks source, emits native code, compiles it in a unique
temporary directory, runs it, and removes temporary files on normal completion.
There is no build cache yet. Repeated runs recompile; use `build` for repeated
execution. Arguments after `--` go to `args()` and are not parsed as compiler
options. Only `run` accepts forwarded arguments; built executables accept them
directly. On Windows, arguments are converted from the wide command line to UTF-8.

`--language-info` prints the compiler's embedded JSON capability document, with
language version, schema version, types, built-ins, diagnostics, limits, and
unsupported features. It needs no source file or native toolchain and works from
an installed compiler without access to this checkout.

`check` performs lexing, parsing, and semantic analysis without invoking C++.
It resolves and checks the entire imported graph. Success produces no output.
`inspect` reports that graph and public interfaces as JSON without running code.
`emit-cpp` writes a self-contained translation unit
including wood's runtime; compile it with a C++23 toolchain to inspect, profile,
or debug backend behavior.

`build` creates a standalone executable. Default output is the input basename
without `.wd` in the current directory, plus `.exe` on Windows. `emit-cpp` defaults
to that basename plus `.cpp`. An explicit `-o` path is used verbatim, including
its extension. Its parent directory must already exist. A source file cannot also
be the output. Imported sources and package manifests/locks are also protected,
and all `.wd` output paths are rejected. Existing outputs are replaced only after successful generation and
compilation, using a staged file in the destination filesystem.

When no script is supplied, commands discover `wood.json` from the current
directory upward. An explicit project directory or manifest is also accepted.
The project entry comes from its manifest; default project artifacts go to
`build/<package-name>[.exe|.cpp]` under its root. `wood init` creates a runnable
project with a reusable module and a test in a new or empty directory. With no
directory argument, or with `.`, it initializes the empty current directory.
Existing populated directories/files and linked targets are rejected without
overwriting them; parents must already exist. Success lists run/build/test next
steps. See [project initialization](projects.md) for layout, name normalization
and I/O-failure behavior. `wood lock`
writes the reviewed local dependency inventory; `--locked` on other commands
requires a matching lock. An existing lock is checked even without `--locked`.
`deps` prints the current inventory; use `deps --locked` to verify it.

`test` discovers `test_*.wd` in the manifest's test directory and compiles/runs
each as an independent native executable. An explicit script runs just that test.
Failures do not prevent the remaining tests from running. `--report=json` writes
one JSON result document to stdout, with captured per-test output and status;
diagnostics use stderr. Any failed test yields status 1. Empty discovery is an
error. See [projects and packages](projects.md) for manifest, lock, inspection,
test schemas, limits, and relative import rules.

Paths with spaces or shell metacharacters are passed as OS arguments. The driver
does not invoke a shell or interpolate source text into commands. Quote paths
according to your invoking shell. Temporary directories are removed with RAII;
forced process termination can leave them behind.

## Native toolchain

Selection order is `--cxx PATH`, `WOOD_CXX`, the C++ compiler recorded when wood
was configured (if still present), then `cl.exe` on Windows or `c++` elsewhere.
`PATH` is a single executable, not a command plus flags. GCC/Clang drivers and
MSVC-compatible `cl`/`clang-cl` names are supported. Renamed compiler wrappers
must accept the corresponding flag style.

The selected compiler's standard library must provide floating `std::to_chars`,
`std::jthread` and `std::stop_token`. CMake checks these prerequisites when building
wood. On Linux, [the GCC and Clang checks](linux.md) test both the compiler and the programs it builds.

The default is `-O2` (`/O2` with MSVC); `-O0` selects `-O0` or `/Od`.
MSVC builds enable `/bigobj` for large generated translation units, including
instrumented programs with many native record/container instantiations. This
changes the temporary object format, not wood syntax or the executable ABI.
The driver does not enable fast math, link-time optimization, CPU-specific instruction sets,
or debug symbols by default. `--sanitize=address` adds address-sanitizer
instrumentation and debug information to native run/build/test executables; the
host toolchain must support it and its runtime must be available. Compiler output is captured; on failure it is shown before the
driver error. Successful execution's stdout contains only the wood program's
output. For custom backend flags, use `emit-cpp` and compile the file yourself.

MSVC requires `INCLUDE` and `LIB` from a Visual Studio developer shell. A CMake
build may locate Visual Studio from a normal PowerShell, but that does not set
the environment for later `wood run` or the integration tests. Open Developer
PowerShell, or use `vcvars64.bat` in a Command Prompt before invoking them.

## Diagnostics and exit codes

`wood explain CODE` prints an explanation of an error: its meaning, common causes
and how to fix it, and for most codes a failing example and one valid correction.
It needs no project, external documentation, Python or C++ toolchain and does not
load a current manifest. Use `wood explain --help` to list available explanations.

The catalog is [Error explanations](reference/explanations.md), which covers every code of the compiler
(`E`), of running programs (`R`) and of the `wood` command (`D`), and each example there is run by a test.
The C++ reference compiler explains only **E3002, E3003, E3005, E3007, E3013, E3014 and E3030**. Codes are case-sensitive uppercase `E`, `D` or `R`
followed by four decimal digits. A missing/malformed argument, unsupported option
or unavailable explanation returns 2. An unavailable code receives no invented
meaning: the message states that missing documentation does not imply an invalid
code, lists available explanations and points to the diagnostic categories below.

Successful output is readable text on stdout (exit 0, empty stderr).
`--diagnostics=json` retains that successful text; it formats **command failures**
as the existing `D5001` diagnostic on stderr. No JSON explanation/report schema
is introduced. `--language-info` lists the catalog under `diagnostics.explain`.
Only `--diagnostics=json` is accepted with a code; standalone `explain --help`
prints usage. Source/project/build options do not apply to this command.
`explain` is a CLI command word, not a new wood keyword. A project directory
named `explain` remains accessible as `wood run explain` or `wood ./explain`.

For E3030, check the specific diagnostic: recursive inference needs a return
annotation on at least one function in the cycle; incompatible or bare/value
returns need compatible returned expressions. An annotation does not make `int`
and `str` interchangeable or create a missing return path. See
[return inference](return-inference.md) for its complete rules.

An uncaught error in a running program exits 70 and reads the same way, with the command that explains it:

```text
script.wd:2:7: runtime error[R4006]: index out of bounds
  help: wood explain R4006
```

Human frontend diagnostics use:

```text
script.wd:2:7: error[E3007]: undefined variable 'answer'
  print(answer)
        ^
```

`--diagnostics=json` emits one JSON object per frontend diagnostic on stderr
(JSON Lines), with `severity`, `code`, `file`, `line`, `column`, and `message`.
Only the first frontend error is reported. The same option covers driver,
filesystem/toolchain, and runtime diagnostics. Driver errors use zero line/column
when no source position is available, as do unlocated R4099 standard-library failures.
Native compiler output is included in a
`D5002` message rather than mixed into the JSON stream. A binary built with this
option retains JSON runtime reporting when run independently. `emit-cpp` also
preserves the selection. Runtime JSON replaces invalid UTF-8 bytes in messages
with U+FFFD and preserves embedded NULs as escapes. Human diagnostics remain the
default. Schema version 1 is described by `--language-info`.

Unknown variables (`E3007`) and functions (`E3005`) may append
`did you mean 'name'?` when exactly one visible, usable name is one insertion,
deletion, substitution or adjacent transposition away. Comparisons are case
sensitive. Names shorter than 3 or longer than 64 bytes are not suggested;
ambiguous or more distant matches are omitted. Qualified function suggestions
stay within the same import alias. Suggestions never change name resolution.

Call-binding errors (`E3003`) show the expected parameter list, including `/`,
`*` and optional defaults (`= ...`), and the number of arguments actually written.
Implicit method receivers and inserted defaults are not counted as provided
arguments. Missing, duplicate, unknown-keyword and positional-only errors identify
the relevant parameter or keyword. Builtin/function-value arity errors show
expected and actual counts. For example:

```text
missing argument 'value' to 'take' (expected take(value: int); provided 0 arguments)
expected int, got str for argument 'value' to 'take'
```

Assignment, return, condition and default-value type mismatches point to the
offending value expression. Argument mismatches point to the supplied expression;
ordinary declared calls also identify the parameter and callee. These refinements
preserve error codes, exit statuses and the six-field JSON schema. Diagnostic
message prose can improve; tools should branch on `code` and use `file`, `line`
and `column` for navigation rather than parsing the wording. Positions use the
expression's existing token location (which can be an operator or call delimiter
for compound expressions), not a source range or automatic fix.

| Exit code | Meaning |
| --- | --- |
| `0` | Command or program succeeded |
| `1` | wood frontend diagnostic |
| `2` | Invalid CLI usage, filesystem error, native toolchain failure, or driver error |
| `70` | Uncaught wood runtime error |
| other | Child program termination status (POSIX signals use 128 + signal); Windows exceptional statuses outside 0..255 map to 2 |

| Codes | Category |
| --- | --- |
| `E1001`–`E1007` | Tabs, indentation, numbers, strings/escapes, parentheses, unexpected characters |
| `E2001`–`E2006` | Syntax, nesting limits, invalid types/expressions and nested functions; E2005 is reserved |
| `E3001` | Reserved/duplicate names |
| `E3002` | Type mismatch or invalid operand/value |
| `E3003`–`E3007` | Arity, invalid range context, unknown function, literal bounds, undefined variable |
| `E3008`–`E3013` | Unreachable code, repeated annotation, invalid iterable, invalid return/loop control, missing return path |
| `E3014` | Empty list/dictionary lacks contextual element or key/value types |
| `E3015` | Invalid mutation target |
| `E2010` | Invalid assignment target syntax |
| `E2011` | Import outside file scope |
| `E2012` | Record outside file scope |
| `E3016` | Invalid error-field access |
| `E3017` | Raise operand must be str or error |
| `E3020` | Dictionary key must be int, str, or bool |
| `E3021` | Infinite direct record containment |
| `E3022` | Unsupported JSON conversion type |
| `E3023` | Invalid or conflicting external C ABI declaration |
| `E3024` | Invalid, conflicting or uninferred generic type parameters |
| `E3025` | Tuple index is not a compile-time literal or is out of bounds |
| `E3026` | Default value is not an allowed literal expression |
| `E3027` | Attempted mutation through a readonly method receiver |
| `E3028` | Invalid closure capture or callable shape |
| `E3029` | Invalid iterator protocol or associated type |
| `E3030` | Recursive or incompatible function return inference |
| `E3031` | Invalid numeric array or numerical operation types |
| `E3100` | Imports require filesystem compilation context |
| `E3101` | Module not found |
| `E3102` | Import cycle |
| `E3103` | Executable statements in imported module |
| `E3104` | Missing/private function export |
| `E3105` | Import alias collision or reserved name |
| `E3106` | Module containment or resource limit |
| `D5001` | CLI/filesystem/process-start driver failure |
| `D5002` | Native compiler returned failure |
| `D5003` | Native compiler/program exceeded its configured deadline |
| `D5004` | Captured compiler/test output exceeded its configured cap |
| `D5005` | Child execution cancelled by a handled interrupt |
| `D5101`-`D5109` | [Package commands](packages.md#diagnostics) |
| `R4001` | Integer overflow |
| `R4002` | Division by zero |
| `R4003` | Numeric conversion or string size outside int range |
| `R4004` | Failed assertion |
| `R4005` | Zero range or slice step |
| `R4006` | Index out of bounds |
| `R4007` | Pop from an empty list |
| `R4008` | File/path/stdin/stdout/stderr I/O failure |
| `R4009` | Empty split/replacement pattern |
| `R4010` | User-raised string error |
| `R4011` | Missing dictionary key |
| `R4012` | JSON parse, extraction, UTF-8, finite-number, or depth error |
| `R4013` | Invalid byte conversion |
| `R4014` | Integer shift count outside 0..63 |
| `R4015` | Invalid sleep duration, native sleep failure or monotonic counter range exhaustion |
| `R4016` | Closed/wrong-direction stream, byte/line limit, file stream I/O or positioning failure |
| `R4017` | Socket setup, resolution, connection, listen, argument or native network failure |
| `R4018` | Socket operation deadline exceeded |
| `R4020` | SQLite dependency, statement, transaction or database failure |
| `R4021` | TLS dependency, trust, handshake or transport failure |
| `R4022` | Native task-operation failure; wait can rethrow original worker errors |
| `R4023` | Attempted to unwrap an absent optional value |
| `R4024` | Attempted to extract an exhausted iterator step |
| `R4025` | Invocation of a moved-from native owned callable |
| `R4026` | Numeric array shape/layout failure |
| `R4027` | Native scalar mathematical domain failure |
| `R4099` | C++ standard-library runtime failure |

Codes are intended to remain stable across compatible releases. Wording and caret
rendering can improve. Columns refer to bytes, not grapheme clusters, so carets
after multibyte text may not visually align in every terminal.

## Process limits

`--timeout-ms N` accepts an integer in 0..86400000. Zero, the compatible default,
means no deadline. The wall-clock limit applies separately to each native compiler
and program child, not to the entire project command. `--max-output-bytes N`
accepts 1..16777216 and defaults to 16777216. It bounds captured combined compiler
or test stdout/stderr while reading pipes, including the bytes retained on disk.
Ordinary interactive `run` program output is inherited and is not capture-limited.

Timeout is D5003, excessive captured output D5004, and handled cancellation D5005.
The driver terminates and reaps children on failures, preserving existing build
artifacts. Windows uses jobs assigned before a suspended child begins execution;
POSIX uses process groups. Descendants are cleaned up when their root finishes.
Ctrl+C/Ctrl+Break on Windows and SIGINT/SIGTERM on POSIX request cancellation while
waiting. These are process-management controls, not an OS sandbox or hard
real-time guarantees; deliberately escaping POSIX groups is outside this model.
Forced termination can leave staging directories, as before. `test --report=json`
records process-limit failures and continues subsequent tests. A capture limit
covers child output, not the overall size of an aggregated project report.

## Native static archives

Repeat --link-library PATH with run, build or test to pass existing .a/.lib
archives to the host linker. Paths resolve against the invoking working directory;
order is retained, including through project test subprocesses. The option does
not accept compiler flags or library search names. Check/inspect/emit-cpp do not
link libraries and reject the option. See [the borrowed C ABI contract](native-ffi.md).
Native archives are trusted code and their hashes are not covered by wood.lock.
