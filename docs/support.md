# Support matrix

This matrix describes platforms and configurations tested for the wood 1.1.0 research release.
See the [known limitations](known-limitations.md) for coverage gaps and [release evidence](stability.md)
for the scope of qualification.

## Platforms

| Platform | Status |
| --- | --- |
| Windows 11 x86-64 | Tested. Releases ship a setup wizard and a portable archive, with the LLVM tools included |
| Linux x86-64 with glibc | Tested on Debian 13. Uses your LLVM 19 tools and GNU ld |
| Other Linux distributions | Untested |
| macOS, ARM, 32-bit, musl | No releases. Some code exists for macOS, but nothing is tested |

Testing covers Windows 11 and Debian 13. A clean machine has not been tested: the installer tests redirect the user
profile and registry instead.

## Which compiler you get

| How you got wood | What `wood` is |
| --- | --- |
| Installer or portable archive | The compiler written in wood, with the LLVM backend. It never falls back to C++: anything the backend can't build fails with B3501 |
| Source build with CMake | The C++23 reference compiler, which emits C++ and needs a C++23 compiler to build programs. Checking and inspecting code needs none |
| Source build, `wood-selfhost` | The compiler written in wood with the C++ emitter, for comparing the two. See the [driver](selfhost-driver.md) |
| Source build, native CLI | The same LLVM compiler the installers ship, built from source. See the [native toolchain](native-cli.md) |

Programs built with the LLVM backend contain no C++ runtime and no C library, except that on Linux, networking, TLS and
SQLite use glibc's loader and threads. The LLVM tools themselves are C++ programs.

## Features

| Status | What |
| --- | --- |
| Ready | The compiler and CLI, projects and `wood.lock`, local libraries, the package manager, the standard library, TCP and HTTP, tasks, the installers and archives, the language server, `wood fmt` |
| Needs a library you install | TLS and HTTPS (OpenSSL 3), SQLite (3.38 or newer). See [optional dependencies](installation.md#optional-dependencies) |
| Experimental | Python packages and the CPython bridge; the GUI, audio, image, process, log and config packages; the VS Code extension |
| Not tested | A clean machine, the extension on Linux and in other editors, audio recording, GUI accessibility, multiple-monitor DPI, ThreadSanitizer on the allocator |
| Not available | Version ranges, a REPL, async syntax, raw-pointer FFI, code signing |

## Details

- Normal builds need neither Python nor any third-party library. Files, numerical code, TCP, HTTP and tasks need none of
  the optional libraries either.
- SQLite and OpenSSL are loaded only when a program uses them, and are never downloaded for you. See the
  [loader rules](backend.md#native-dependencies).
- Package versions are exact. There is no version-range solver.
- Python support is experimental and needs CPython 3.14 with the GIL, which `wood add python@...` installs per project.
  A wood installation contains no Python. See [Python packages](python-distribution.md).
- `std.task` gives you threads you join. There is no async syntax or event loop.
- C bindings take scalar values and borrowed numeric buffers. Raw pointers, structs, variadic functions and header import
  aren't available. See [calling C](native-ffi.md).
- The full list of language limits is in [the reference](language.md#current-boundaries).

wood is not a sandbox. Checked arithmetic and value semantics don't protect you from a malicious library, running out of
memory, or a program's access to your files. Read the [security policy](../SECURITY.md) before handling untrusted input.
