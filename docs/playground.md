# The browser playground

The playground runs real wood in a browser. There is no JavaScript version of the language: your source goes through wood's own
compiler, and the program runs on wood's own runtime, both as WebAssembly. Nothing is sent to a server.

It is for learning and for trying things out. The repository has its runtime and tests, not a page to host it. For anything that touches your machine or the network, [install wood](installation.md).

## How it works

1. **The compiler is the real one.** The lexer, parser, type checker and LLVM lowering are the same wood code as a native build, so errors have the
   same codes and positions.
2. **A small last step.** Native builds hand LLVM IR to LLVM. A browser has no LLVM, so `bootstrap/src/emit_wasm.wd` turns that same IR into a
   WebAssembly module.
3. **The runtime is the real one.** Native integers, lists, strings and errors come from `runtime/native`, compiled to WebAssembly. Pointers stay 64-bit,
   so every layout and offset matches a native build.

Each run gets a new memory, a new table and a new set of files.

## What works

The whole core language: variables, functions, conditions, loops, lists, dictionaries, tuples, optionals, records, generics, closures, iterators and error
handling, with checked integers and checked indexing.

| Works | Modules |
| --- | --- |
| Fully | `std.binary`, `std.collections`, `std.csv`, `std.functional`, `std.geometry`, `std.iter`, `std.linalg`, `std.math`, `std.num`, `std.path`, `std.random`, `std.stats`, `std.utf8` |
| In memory | `std.fs`, `std.io`, `read_text` and `write_text` use a private folder that disappears with the run |
| Limited | `std.time` works, and `sleep` keeps the browser's worker busy while it waits. `std.env` sees an empty environment |
| Not available | `std.net`, `std.http`, `std.https`, `std.tls`, `std.sqlite`, `std.python`, `std.task`, Python packages and every optional package |

Modules that are not available still compile, since the checker is the same one. They raise an ordinary wood error when used, so `try`/`except` sees it.
Standard input is always empty, and programs have no command-line arguments.

## The sandbox

A program can only do what WebAssembly and one host function allow. That function answers a short list of system calls from an in-memory table, in
`tools/playground/sandbox.js`, and refuses the rest. `tools/playground/worker.js` runs it in a web worker. There is no route to the network, your files, or the page.

| Limit | Value |
| --- | --- |
| Running time | 30 seconds, then the worker is ended |
| Compiling | 60 seconds, then the worker is ended |
| Memory | 256 MB |
| Output | 1 MB |
| Private files | 16 MB stored at once. Rewriting or removing a file frees its space |
| Stack | Whatever the browser allows, plus 4 MB for wood's own frames |

Reaching a limit stops the program and says which limit it was. Very deep recursion stops sooner than it would natively.

## Differences from native wood

- Line endings are always `\n`, as on Linux, even when your computer runs Windows.
- A program is one file, so it cannot import modules of its own.
- A program's files are in `/work`, and a compile error names `/work/main.wd`.
- The largest array a program can ask for is limited by the browser's memory, so a request that fails natively with one error code can fail here with another.
- Programs run more slowly than native ones, since this path is built to be simple rather than fast.

## Testing it

`tests/playground/examples.mjs` runs every program in `tools/playground/examples` through this path and through native wood, and requires the same
output, status and error positions. It also checks the memory, output, stack and file limits above. `tests/playground/differential.mjs` does the same
comparison for other programs.

## Building it

`tools/playground/build.sh` builds the compiler and runtime modules. It needs a native wood, clang 19 with the WebAssembly target and `wasm-ld` 19:

```sh
tools/playground/build.sh build/playground
node tests/playground/examples.mjs wood build/playground
```
