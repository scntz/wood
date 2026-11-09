# Changelog

## 1.1.0

Additions only: code written for 1.0.0 keeps working. The language is still 0.4.0 and the native runtime ABI is still 4.
See the [release notes](docs/releases/1.1.0.md).

Language

- Tuple unpacking: `q, r = divide(17, 5)`, `a, b = b, a`, and `for name, count in pairs:`. The tuple's size is checked at
  compile time.
- Optional narrowing: after `if x is not None:`, or after `if x is None: return`, `x` has its plain type, so `unwrap` is
  no longer needed there.
- `try` takes a `finally:` block that runs however the statement is left, with or without an `except` block.
- `error(code, message)` makes an error with the program's own code, such as `app.not_found`.
- A bare `raise` in an `except` block re-raises the error being handled.
- `char_count(text)`, `chars(text)` and `char_slice(text, start, stop)` work on UTF-8 characters. `str`, `len` and
  indexing still work on bytes.

Tools

- `wood fmt` formats `.wd` files in place, and `wood fmt --check` lists the files that would change. The language
  server formats documents the same way.

Performance

- List and array indexing checks bounds and ownership inline, so loops that read and update them run 2 to 13 times
  faster (N-body 2356 to 178 ms). Dictionaries are unchanged. See [performance](docs/performance.md).

## 1.0.0

The first stable release, for Windows and Linux x86-64. The language is 0.4.0 and the native runtime ABI is 4.
See the [release notes](docs/releases/1.0.0.md) and [stability and compatibility](docs/stability.md).

Language and compiler

- A compiler written in wood that rebuilds itself in three stages, with stage 2 and stage 3 identical, and emits LLVM IR.
- Checked 64-bit integers and indexing, and values that copy on write. Writing `values[i] = values[i] + 1` updates the
  element in place rather than copying the collection.
- Errors name the file, line, column and a code, such as `main.wd:4:9: runtime error[R4006]: index out of bounds`.
  `--diagnostics=json` gives JSON, and `wood explain CODE` describes every compiler, runtime and command error.
- A stack overflow reports R4028 and exits with code 70.
- Names from other languages, such as `input` and `println`, get a hint.

Standard library and packages

- 22 native modules, covering files, JSON, CSV, typed arrays, math, TCP and HTTP, and tasks. `std.math` has the usual
  elementary functions and the constants `pi` and `e`.
- `wood add` takes catalog packages, GitHub repositories and Python packages, and `wood.lock` pins every source.
- `wood_image` can render a `std.geometry` mesh with `scene`.

Tools

- `wood lsp` and a VS Code extension, with the compiler's own diagnostics, completion, navigation and rename.
- A Windows setup wizard with the LLVM tools included, a Linux installer, and portable archives.
- On Windows, `wood add` and `wood build` never run a program from the current directory, and refuse `.bat` and `.cmd`
  files, because `cmd.exe` would parse their arguments.

Benchmarks

- Nine programs comparing wood with C++, Rust, Go, Java and Python, with every sample published in `benchmarks/published/`.
