# The native toolchain

The installers and archives give you the native toolchain: the compiler and driver written in wood, building through LLVM.
It shares its frontend, project rules and commands with `wood-selfhost`, and is separate from the C++23 reference compiler
you get from a source build.

## How it differs

`bin/wood` (`wood.exe` on Windows) has the usual commands: `run`, `build`, `check`, `test`, `init`, `lock`, `deps`, `inspect`,
`explain`, `add` and the rest. `run`, `build` and `test` emit LLVM IR, verify it, generate objects and link an executable,
with no C++ involved. If a program uses something the LLVM backend can't build, it fails with B3501. It never falls back to C++.

- `--backend cpp|llvm` chooses the backend. The native toolchain defaults to `llvm`; a source build defaults to `cpp`.
- `emit-llvm` writes checked IR without verifying it or building anything.
- `check` checks the program. It doesn't prove the LLVM backend can build it.
- `--language-info` describes the language, plus `driver_default_backend`, `driver_backends`, `driver_native_runtime_abi`
  and related fields for the driver. For what the backend supports, see
  [`native-backend-contract.json`](../bootstrap/native-backend-contract.json).
- `std.python` is listed under `optional_standard_modules`, as experimental and LLVM only. See [Python packages](python-distribution.md).

## Install from source

You need CMake 3.25 or newer, a seed compiler, LLVM 19's `opt` and `llc`, a linker, and a runtime object built with Clang
as C17. Python isn't needed. On Linux, with your own paths:

```sh
cmake -E make_directory out
cmake -DREFERENCE=/absolute/path/to/wood \
  -DOUTPUT="$PWD/out/native-assets" -P tools/prepare-native-assets.cmake
cmake -DCLANG=/usr/bin/clang-19 -DTARGET=linux-x86_64 \
  -DOUTPUT="$PWD/out/native-managed.o" -P tools/prepare-native-runtime.cmake
cmake -DEMITTER=/absolute/path/to/wood-native \
  -DASSETS="$PWD/out/native-assets" -DRUNTIME_OBJECT="$PWD/out/native-managed.o" \
  -DTARGET=linux-x86_64 -DLLVM_DIR=/absolute/path/to/llvm/bin \
  -DLINKER=/usr/bin/ld -DPREFIX="$HOME/wood-native" \
  -P tools/install-native.cmake
```

- `REFERENCE` is any working `wood`. It only exports the language and error catalogs.
- `EMITTER` is the emitter described in [the LLVM backend](native-backend.md#building-by-hand). To build with an installed
  native `wood` instead, add `-DSEED_IS_DRIVER=ON`.
- `PREFIX` must not exist yet, and its parent must. The install is staged next to it and moved into place when complete. It
  changes no PATH, registry or system settings.

The result is `bin/wood` and `share/wood/`, which holds the standard library, metadata, the runtime IR and
`native/managed.o`, the compiler's source, tests, examples, docs and licenses. `share/wood/native.json` records the target,
ABI, LLVM directory and linker. The prefix can be moved on the same machine; on another machine, point it at that machine's
LLVM and linker.

On Windows, use `TARGET=windows-x86_64`, a COFF runtime object and the SDK linker. You can use Windows LLVM 19.1.7, or
LLVM from WSL with `-DWSL_DISTRIBUTION=Debian` and a Linux `LLVM_DIR`; WSL is used only when you ask for it. The installers
avoid all of this by bundling the LLVM tools and an import library for Kernel32.

A native-only install has no C++ prelude. For `--backend cpp`, pass `-DCPP_RUNTIME=/path/to/runtime.hpp` and have a C++23
compiler available.

## Use it

```sh
/absolute/prefix/bin/wood init sample
cd sample
/absolute/prefix/bin/wood run -- "world"
/absolute/prefix/bin/wood build -O2
/absolute/prefix/bin/wood test --report=json
/absolute/prefix/bin/wood lock
/absolute/prefix/bin/wood check --locked --diagnostics=json
/absolute/prefix/bin/wood emit-llvm -o sample.ll
```

Projects, dependencies and `wood.lock` work exactly as described in [projects](projects.md). A dependency whose bytes
changed fails the build rather than refreshing the lock, and an output path can never overwrite a source file or project
metadata.

You can override the configured tools with `--native-home DIR`, `--llvm-dir DIR`, `--linker PATH` and, on Windows,
`--llvm-wsl DISTRIBUTION`. `--sanitize=address` isn't available with the native toolchain.

## Optional service dependencies

SQLite needs a thread-safe SQLite 3.38 or newer, and TLS needs OpenSSL 3 (`libcrypto.so.3` and `libssl.so.3` on Linux,
`libcrypto-3-x64.dll` and `libssl-3-x64.dll` on Windows). Both are loaded on first use, never downloaded, and follow the
[loader rules](backend.md#native-dependencies): on Windows, only System32 or an absolute `WOOD_OPENSSL_DIR` or
`WOOD_SQLITE_DIR`. Bring your own trust roots, either a PEM bundle or OpenSSL's defaults; they aren't necessarily the
Windows certificate store. The [TLS rules](backend.md#tls-and-https) apply: TLS 1.2 or newer, full certificate and
hostname checks, and no insecure fallback.

TCP and HTTP need no library: Windows uses Winsock from System32, and Linux the system resolver.

On Linux, a program that uses sockets, SQLite, TLS or the Python bridge starts through glibc's loader
(`/lib64/ld-linux-x86-64.so.2`) and uses real pthreads for tasks, so that the resolver and those libraries get a properly
initialized C runtime. Everything else, including the compiler, stays static with no C library.

[`examples/sqlite_inventory.wd`](../examples/sqlite_inventory.wd) is a complete example. Run it with
`wood run examples/sqlite_inventory.wd -- inventory.db stock.csv`, where the CSV has a `name,quantity` header. Each import
commits as a whole, and an invalid quantity rolls it back.

## Tests

```sh
cmake -G Ninja -S tests/native -B out/native-validation \
  -DINSTALLED_CLI=/absolute/prefix/bin/wood \
  -DEMITTER=/absolute/path/to/wood-native \
  -DSTDLIB=/absolute/prefix/share/wood/stdlib.json \
  -DRUNTIME_OBJECT=/absolute/prefix/share/wood/native/managed.o \
  -DLLC=/absolute/path/to/llvm/bin/llc -DOPT=/absolute/path/to/llvm/bin/opt \
  -DLINKER=/usr/bin/ld -DPRODUCTION=/absolute/path/to/reference/wood \
  -DCLANG=/usr/bin/clang-19 -DENABLE_NATIVE_BOOTSTRAP=ON
ctest --test-dir out/native-validation --output-on-failure -j 1
```

These rebuild the toolchain with itself three times and compare stages 2 and 3, install it, check that it refuses to
overwrite and still works after moving, and run projects and applications through it. `PRODUCTION` adds comparisons with the
reference compiler. On Windows, `INSTALLED_CLI` alone runs the driver tests; `tests/native/windows.ps1` adds the Windows
binary checks.

`native_tasks` uses the C fixture `tests/native/task_fixture.c` (on Windows, pass it as `-DTASK_LIBRARY=...`) to test
concurrent callbacks, cancellation and worker errors. `native_network` checks the socket declarations against the SDK and
runs IPv4, IPv6, DNS and HTTP client and server tests. `native_sqlite` runs the inventory example at both optimization
levels. With Clang on Linux, these also run under AddressSanitizer, UBSan and ThreadSanitizer.

The driver talks to the compiler through a private bridge that bounds requests to 64 MiB and responses to 64 KiB, runs
child processes without a shell, and stages files in random directories it cleans up. Cleanup is best effort: a killed
process can leave a staging directory behind.

## Packaging

```sh
cmake -DPREFIX=/absolute/native-prefix \
  -DOUTPUT=/absolute/output/wood-native-linux-x86_64.tar.gz \
  -DSOURCE_DATE_EPOCH=1704067200 -P tools/package-native.cmake
```

Set `SOURCE_DATE_EPOCH` to the commit's timestamp (`git show -s --format=%ct HEAD`). The packager writes a `wood-native`
directory with a `MANIFEST.sha256`, normalizes file times, and checks the extracted bytes. Building twice gives identical
archives. `-DPORTABLE_NATIVE_TOOLS=ON` makes the package find LLVM and the linker on PATH instead of using this machine's
paths. `tools/qualify-native-consumer.cmake` then checks a package from the outside: projects, locked tests, the OBJ and
SQLite examples, and TCP and TLS tasks.

The Windows installer adds the LLVM tools and a Kernel32 import library on top of this; see
[the installer build guide](../tools/installer/README.md).
