# wood for coding agents

This guide is for agents writing applications in wood, with any coding tool. You need no SDK, account, Python or
model-specific setup. The rules for changing wood itself are in [AGENTS.md](../AGENTS.md); don't copy them into an
application.

## Find out what you have

The installers and archives give you `wood` built with LLVM, which needs no C++ compiler or developer shell. A source build
gives you the C++23 reference compiler, which does. Neither switches to the other on its own. The
[support matrix](support.md) explains the difference.

1. Run `wood --version`, `wood --help` and `wood --language-info`.
2. Read the [language reference](language.md), the [API index](stdlib-api.md) and the [support matrix](support.md). Don't
   assume a feature exists because it's on the roadmap or in Python.
3. Read `wood.json` and the existing source and tests. If there's a lock, run `wood deps --locked` and
   `wood inspect --locked`. Inspecting reports the public interface without running anything.
4. Use the installation's default backend unless the task says otherwise.

`--language-info` prints one JSON document with `schema_version`, `version`, `types`, `builtins`, `commands`,
`standard_modules`, diagnostic fields, limits and unavailable features. Check the fields that are actually there. The
[language.json](language.json) in the repository may be newer than the compiler you have.

`wood inspect source.wd` or `wood inspect project --locked` prints JSON with the entry, modules, imports, hashes, public
functions and records, parameters and types. Identify types by their `type_ref` or `returns_ref` module and name, since
display names can be ambiguous. A generic function's result may be unresolved; don't guess it. The format is described in
[projects](projects.md).

## A reliable loop

```sh
wood init hello
wood check hello --diagnostics=json
wood lock hello
wood deps hello --locked
wood inspect hello --locked
wood test hello --locked --report=json
wood run hello --locked -- world
wood build hello --locked -o hello-app
```

On Windows the executable is `hello-app.exe`. `check`, `inspect`, `deps`, `lock`, `init` and `explain` never run a native
compiler; `run`, `build` and `test` do. `run` recompiles every time, so build once if you run a program repeatedly.

`init` needs a new or empty directory. Put modules under the manifest's source root, and `test_*.wd` scripts under its test
root. Import dependencies by the names declared in `wood.json`. Add packages with `wood add`, which takes exact versions only.
If a lock check fails, look at what changed in the dependency before running `wood lock`: never relock just to make the
error go away.

`--timeout-ms N` and `--max-output-bytes N` limit child processes. They aren't a sandbox or a memory limit. Link trusted C
libraries with `--link-library PATH`, which the lock doesn't pin.

## Syntax and idioms

```wood
import std.iter as it

record Product:
    name: str
    cents: int

fn total(items: list[Product]) -> int:
    return it.fold(items, 0, fn(amount: int, item: Product) => amount + item.cents)

products = [Product("book", 1200), Product("pen", 200)]
assert(total(products) == 1400)
print(json_dump(products))
```

- Functions use `fn`, and blocks use indentation and `:`. `pass` makes an empty block. There are no braces, semicolons,
  `let`, `var` or `main()`.
- Parameters need types. A local gets its type from its first assignment, and return types are inferred where it's safe;
  recursive functions need an annotation.
- The types are `int`, `float`, `bool`, `str`, `list[T]`, `dict[K,V]`, `tuple[A,B]`, `T | None`, records, arrays, `json`
  and function types.
- Set up accumulators before a branch or loop. Functions can't see script variables, so pass them in. Closures,
  `fn(x: T) => expression`, capture copies; `[mut name]` captures a copy you can change.
- `record Box[T]:` and `fn identity[T](value: T) -> T:` are generic. `fn read(self)` reads a record; `fn update(mut self)`
  changes the caller's variable.
- Named functions and constructors accept defaults and keywords. Function values and built-ins take positional arguments
  only. Defaults must be literals, and there are no variadic functions or argument unpacking.
- Write `values: list[int] = []` when an empty collection's type can't be inferred.
- Lists, dictionaries, arrays and records are values: changing a parameter doesn't change the caller's copy. Return the new
  value, or use a `mut self` method.
- Strings are bytes. `len("á")` is 2. Use `char_count`, `chars` and `char_slice` for characters, since indexing and
  slicing can split them.
- `int` is a checked 64-bit integer. `/` gives a float, and `//` is floor division. Conditions must be `bool`: use `and`,
  `or` and `not`, with no truthiness.
- Index tuples with literal numbers, or unpack them: `q, r = pair` and `for name, count in pairs:`. After
  `if x is not None:`, or after an `if x is None:` that returns or raises, `x` has its plain type without `unwrap`.
- `std.iter` functions take the source first; `std.functional` takes the callback first. Check the signature.
- `read_text` and `write_text` read and write whole files as bytes. For streams use `std.io`, where short reads are normal
  and lines keep their LF.
- CSV functions need an explicit one-byte delimiter. Extract JSON values explicitly with `json_dict`, `json_str` and the rest.

## Errors and debugging

Use `try` / `except err` / `finally:`, `raise "message"`, `raise err`, `raise error("app.code", "message")` for your own
codes, and a bare `raise` in a handler to pass the error on. An error has `code`, `message`, `file`, `line` and `column`.
There are no exception classes. `with expression as handle:` closes streams, databases,
statements, transactions, TLS contexts and tasks however the block exits. Copies of a handle share it, so closing one
closes all of them, and returning a handle from a `with` block returns it closed. See [errors](errors.md).

Run `wood check app.wd --diagnostics=json` first. stderr has one JSON object per line, with those same fields. Positions are
one-based bytes, or 0 if unknown. Fix the first error and check again. "Did you mean" suggestions are hints: don't apply
them blindly, and don't parse the English messages.

Exit codes are 0 for success, 1 for errors in the program (or failed tests), 2 for driver or toolchain failures and 70 for an
uncaught runtime error. Programs can exit with other codes. `test --report=json` prints one JSON document on stdout with each
test's status, output, exit code and `driver_error`. A timeout, a partial run or a successful `check` isn't a passing test.

`wood explain CODE` explains any error code offline. A program built with `--diagnostics=json` reports runtime errors as JSON.

## What isn't there

Check the [API index](stdlib-api.md). `os`, `requests`, `numpy` and `asyncio` aren't wood modules. For processes, use the
[`wood_process` package](official-packages.md).

The experimental [Python bridge](python-distribution.md) uses `import python@module`, with explicit `.call` and `.attr`
and `std.python` conversions. Use it only when the installation lists it, and don't translate Python code line by line.
There's no live registry, REPL, async syntax, raw-pointer FFI or ML framework. wood aims to be as easy as Python,
not compatible with it.

TLS, HTTPS and SQLite load their libraries when used, and the user installs them. Never turn off TLS verification or switch
backends to make a test pass. wood isn't a sandbox: native libraries are trusted code, and filesystem checks don't stop
another process changing files.

## Instructions for an application project

You can adapt this as an application's `AGENTS.md` or another tool's instruction file:

```text
This is a wood application. Use fn and the language of the installed compiler.
Run wood --version, --help and --language-info, and read the matching language and API docs.
Read wood.json, the existing modules and tests before editing. Keep changes focused.
Check with wood check --diagnostics=json, reading stderr as JSON lines by code and position.
Format changed files with wood fmt.
Review dependency changes before wood lock, and verify with wood deps --locked.
Run wood test --locked --report=json, then the application itself with realistic input.
Keep value semantics, checked operations and with-scopes for resources.
Don't invent Python APIs, missing language features or package downloads.
Report the commands and platform, which checks passed, failed or didn't finish, and what is still missing.
```

For a new application with no lock, review the source and dependencies and create the lock on purpose before using
`--locked`. See [troubleshooting](troubleshooting.md) when something fails.
