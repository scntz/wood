# Announcing wood 1.0.0

Text for the launch post. Use it as it is, or cut it down. Everything here matches the
[release notes](../../docs/releases/1.0.0.md) and [known limitations](../../docs/known-limitations.md).

## GitHub description

A native programming language. Built for humans, designed for AI.

## Short version

wood is a native programming language with indentation-based syntax and static types. It compiles to
standalone executables through a compiler written in wood. wood 1.0.0 is out for Windows and Linux x86-64,
with a package manager, a language server and a VS Code extension.

## Full post

**wood 1.0.0 is out.** wood is a native programming language: simple syntax, static types, and
standalone executables with no runtime to install.

```wood
record Product:
    name: str
    cents: int

fn total(items: list[Product]) -> int:
    amount = 0
    for item in items:
        amount = amount + item.cents
    return amount

products = [Product("book", 1200), Product("pen", 200)]
print(total(products))
```

What you get:

- A compiler written in wood, which rebuilds itself through a three-stage bootstrap that must reproduce
  identical bytes. Ordinary builds need no C++ compiler and no Python.
- Checked 64-bit integers and indexing, and values that copy on write.
- A standard library of 22 native modules, covering files, JSON, CSV, typed arrays, math, TCP and HTTP, and tasks.
- Runtime errors that read well, with the file, line and code, and `wood explain` for every code.
- `wood add` for catalog packages, GitHub repositories and Python packages, with a lockfile that pins every source.
- `wood lsp` and a VS Code extension, with the compiler's own diagnostics, completion, navigation and safe rename.
- Published benchmarks against C++, Rust, Go, Java and Python, including the tests wood loses.

It is tested on Windows 11 and Debian 13 x86-64, and Windows installers are not code signed yet. The
[known limitations](../../docs/known-limitations.md) list what is not done.

Download: https://github.com/scntz/wood/releases/tag/v1.1.0

Start with the tutorial: https://github.com/scntz/wood/blob/main/docs/tutorial.md

Built for humans, designed for AI.
