<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo-dark.svg">
  <img src="docs/assets/logo.svg" alt="wood logo" width="88">
</picture>

# wood

**Built for humans, designed for AI.**

wood is a native programming language with indentation-based syntax, static types and value
semantics. It compiles to standalone executables through a compiler written in wood.

> [!NOTE]
> **wood is an open-source research project** exploring a self-hosted native language, checked
> operations, and tooling for people and coding agents. **1.1.0 is the latest published release.**
> There is no regular release schedule or commitment to fixes, security updates, or support.
> The source and release artifacts remain available to study, reproduce, and fork.
> See the [release evidence and compatibility scope](docs/stability.md).

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

[Tutorial](docs/tutorial.md) · [Language reference](docs/language.md) · [Standard library](docs/stdlib.md) ·
[Packages](docs/packages.md) · [All documentation](docs/README.md)

## What you get

- A compiler written in wood, which rebuilds itself and reproduces the same bytes. You need no C++ compiler and no Python.
- Checked 64-bit integers and indexing, and values that copy on write. Files, sockets, databases and tasks close when their
  scope ends.
- Tuple unpacking, optionals that narrow after a `None` check, and `try`, `except` and `finally` with error codes of
  your own.
- A standard library with collections, generics, closures, lazy iterators, JSON, CSV, UTF-8 characters, files, typed
  arrays, TCP and HTTP, tasks, and optional TLS and SQLite.
- One package manager: `wood add` takes catalog names, GitHub repositories and Python packages, and `wood.lock` pins them.
- `wood fmt`, which formats every file in one layout, `wood lsp` for any editor that speaks LSP, and a VS Code
  extension.
- Output other tools can read: JSON diagnostics, `wood inspect`, `--language-info` and offline `wood explain`.

## New in 1.1.0

```wood
fn lookup(prices: dict[str, int], name: str) -> int | None:
    if name in prices:
        return prices[name]
    return None

fn price(prices: dict[str, int], name: str) -> int:
    cents = lookup(prices, name)
    if cents is None:
        raise error("shop.unknown_item", "no price for " + name)
    return cents  # an int here, no unwrap needed

prices = {"book": 1200, "pen": 200}
for name, count in [("book", 2), ("lamp", 1)]:
    try:
        print(name, price(prices, name) * count)
    except err:
        print(err.code, err.message)
    finally:
        print("checked", name)

print(char_count("café"), len("café"))  # 4 characters, 5 bytes
```

- **Unpacking:** `q, r = divide(17, 5)`, `a, b = b, a` and `for name, count in pairs:`.
- **Narrowing:** after a `None` check, an optional has its plain type.
- **Errors:** `finally:` runs however a `try` is left, `error(code, message)` creates errors with your own codes, and a
  bare `raise` passes an error on.
- **Characters:** `char_count`, `chars` and `char_slice` work on UTF-8 characters, while `str`, `len` and indexing stay
  in bytes.
- **`wood fmt`:** formats `.wd` files, and `--check` lists the ones that would change. The language server and the VS
  Code extension format the same way.
- **Speed:** loops that read and update lists and arrays run 2 to 13 times faster.

Code written for 1.0.0 builds and runs the same. See the [release notes](docs/releases/1.1.0.md).

## Install

Download from the [latest release](https://github.com/scntz/wood/releases/tag/v1.1.0), with
`SHA256SUMS` to verify the files.

**Windows x86-64.** Run `wood-setup.exe` and keep **Add to PATH** selected. LLVM tools are included, so
you need no Visual Studio or Python. The installer is not code signed, so Windows shows a publisher
warning. See [Windows setup](docs/windows-setup.md).

**Linux x86-64.** Tested on Debian 13. wood uses your LLVM 19 tools and GNU ld:

```sh
sudo apt install llvm-19 binutils
export PATH="/usr/lib/llvm-19/bin:$PATH"
sha256sum -c SHA256SUMS --ignore-missing
chmod +x wood-setup-linux-x86_64
./wood-setup-linux-x86_64 install "$HOME/wood"
```

Setup prints the `PATH` line to add to your shell profile. [Installation](docs/installation.md) also
covers portable archives and optional dependencies.

**VS Code.** Install `wood-1.1.0.vsix` from the same release:

```sh
code --install-extension wood-1.1.0.vsix
```

## Your first program

Open a new terminal and create a file called `main.wd`:

```wood
print("hello, wood")
```

Run it, or build an executable that runs without wood:

```sh
wood run main.wd
wood build main.wd -o hello
```

`wood fmt main.wd` puts a file in the standard layout, and `wood fmt --check` reports files that need it.

## Projects and packages

Packages and tests belong to a project. For the downloadable 1.1.0 compiler, save
[wood-catalog.json](https://github.com/scntz/wood/releases/download/v1.1.0/wood-catalog.json)
next to the directory where you will create `hello`. See [package setup](docs/packages.md#catalog-for-the-11-release).

```sh
wood init hello
cd hello
wood run
wood add wood@image --index ../wood-catalog.json
wood test
wood build -o hello-app
```

`wood add` also takes `github@owner/repository#v1.2.0` and `python@numpy`. It runs no install scripts, and
`wood.lock` records exact sources, so tampering fails until you deliberately relock. See
[how packages work](docs/packages.md) and [publishing packages](docs/package-publishing.md).

## Examples

[CSV report](examples/sales_report.wd), [multi-package project](examples/project),
[log processor](examples/log_report.wd), [mesh transformer](examples/mesh_import.wd) and
[Media Desk](examples/media_desk), a desktop application. See [all examples](docs/examples.md).

[Troubleshooting](docs/troubleshooting.md) covers common problems, and coding agents have [their own guide](docs/agents.md).

## Status

wood 1.1.0 implements language 0.4.0 on native runtime ABI 4, and is tested on Windows 11 and Debian 13, both x86-64.
Read the [release notes](docs/releases/1.1.0.md), [stability and compatibility](docs/stability.md), the
[known limitations](docs/known-limitations.md) and the [support matrix](docs/support.md).

## Build from source

You need CMake 3.25+ and a C++23 compiler: GCC 14 or Clang 19 on Linux, or MSVC in an x64 developer
shell on Windows. A source build gives you the C++ reference compiler and test suites. The
[native toolchain](docs/native-cli.md) that releases ship is built separately.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWOOD_PYTHON_TESTS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

On Windows, use `cmake -S . -B build -A x64 -DWOOD_PYTHON_TESTS=OFF`, then `--config Release` to build
and `-C Release` for CTest. To reproduce the release files, see [releasing](docs/releasing.md).

## Repository

| Path | Contents |
| --- | --- |
| `bootstrap/` | The self-hosted compiler, CLI, package manager and language server, in wood |
| `src/`, `include/` | The C++23 reference compiler |
| `runtime/` | The native and reference runtimes |
| `stdlib/`, `packages/`, `catalog/` | The standard library, official packages and catalog |
| `editors/vscode/` | The VS Code extension |
| `examples/`, `tests/`, `benchmarks/` | Example programs, [test suites](tests/README.md) and measurements |
| `tools/` | [Build, packaging and release scripts](tools/README.md) |
| `docs/` | Guides and references |

## Continuing wood

Fork wood to continue the research or maintain your own version. [CONTRIBUTING](CONTRIBUTING.md) and the
[architecture](docs/architecture.md) explain how the code fits together and how to test a change. There is no one to
report vulnerabilities to here: read the [security policy](SECURITY.md) before relying on wood.

## License

MIT OR Apache-2.0, at your option. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).
