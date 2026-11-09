# The LLVM backend

The installers and archives ship a compiler written in wood that emits LLVM IR, runs LLVM's code generator and links the
result itself. Programs it builds contain no C++ and no C library: they talk to the OS directly, through a small runtime
written in freestanding C17. In a source build, the C++ reference compiler and its C++ backend stay the default.

The compiler builds itself this way on Windows and Linux x86-64. Each stage compiles the next, and stage 2 and stage 3
produce byte-identical IR and executables. The [native CLI](native-cli.md) wraps the same backend in the normal `wood`
commands. [`bootstrap/native-backend-contract.json`](../bootstrap/native-backend-contract.json) is the machine-readable
summary of what the backend supports.

The loader, type checker, generic specialization, diagnostics and lock verification all run before lowering, so a program
the backend accepts has been checked exactly as the reference compiler checks it. Only reachable functions are emitted,
but every function is checked.

## What it supports

- Checked 64-bit integers, binary64 floats, `bool`, `None` and ranges.
- Numeric `array[int]` and `array[float]` of rank 1 to 8: checked indexing, slices and transposes that share storage,
  reshape, copy on write, iteration, and C17 kernels for elementwise arithmetic, compensated sums and dot products, matrix
  multiply and affine transforms.
- Byte strings, with the usual slicing, searching, splitting and conversion, exact decimal-to-float parsing and shortest
  round-trip float formatting.
- Lists and dictionaries, with copy-on-write snapshots. Dictionary keys are `int`, `str` or `bool`. Dictionaries keep
  insertion order with an AVL index, so lookup and insertion take O(log n) comparisons.
- Records, generic records, methods, tuples, optionals, errors, function values, closures and `callable`.
- `if`, `while`, `for`, comprehensions, `try`/`except`/`raise`, `assert`, and custom iterators with `iter` and `next`.
  `std.functional` and `std.iter` run natively, including lazy `map`, `filter`, `take` and `zip`.
- File streams, buffered line reads, `with` scopes, paths, directories and files.
- TCP and HTTP over IPv4 and IPv6, with deadlines, half-close and cooperative cancellation. See [networking](networking.md).
- TLS and HTTPS through OpenSSL 3, and SQLite, both loaded when used. See [backend services](backend.md).
- Tasks: joinable worker threads that exchange immutable JSON, with up to 256 running at once. `task_spawn_io` hands a
  connected TCP or TLS stream to a worker.
- `std.time`, stdin, `args()`, JSON and printing, with the same diagnostics as the reference compiler. An uncaught error
  exits with code 70.
- Full-range `sin` and `cos` from six musl 1.2.5 kernels, with no libm. Results may differ from another platform's libm in
  the last bit. See [the math runtime notes](../runtime/native/math/README.md).
- Calling C with integers, doubles, byte strings, scalar callbacks and contiguous numeric buffers. See
  [calling C](native-ffi.md#experimental-llvm-path).

Anything outside this fails with B3501 at the line that uses it, and emits nothing. There is no fallback to C++.

## The runtime and ownership

Runtime ABI 4 is a private contract between the emitter, the runtime IR and the runtime object. Keep the three together:
every object carries a `wd_native_runtime_abi4` marker, and older objects fail to link.

- Strings are data, length and owner. Literals have no owner. Records, tuples, optionals and errors retain and release their
  fields; lists and dictionaries copy on first shared write.
- Reference counts and copy-on-write checks are atomic. Each task has its own context and values, and only immutable JSON
  crosses between tasks. C callbacks use a per-thread scope stack.
- Arrays are a descriptor plus a typed buffer. Views share the buffer. A write checks its coordinates first, then copies a
  shared buffer into dense order. Reductions keep the documented order, and the C kernels disable floating-point contraction.
- Temporaries are released at the end of each full expression, and every exit path closes `with` scopes and releases
  locals. A whole-statement assignment releases its own read pins just before writing, so `x[i] = x[i] + 1` doesn't copy `x`.
- Methods borrow their receiver only when later arguments can't change it, and mutations re-check indexes after evaluating
  arguments.
- Closures are LLVM structs with owned captures. `callable` is a reference-counted box with a typed thunk.
- Iterator steps carry a presence flag, so a present `None` is different from the end. Range iteration checks for overflow.
- Streams share identity: closing one alias closes them all, and a `with` scope closes the handle it entered even if the
  name is reassigned. Cleanup errors surface on normal exits but never replace an earlier error.
- Line reads keep LF, CR and NUL bytes, use a buffer of at most 4096-byte reads, and reject lines over the limit with R4016
  rather than truncating them.
- JSON input is limited to 16 MiB and 64 levels of nesting. Duplicate keys, bad UTF-8, bad escapes and bad numbers raise R4012.
- Path canonicalization follows links, bounded to 40 hops on Linux. On Windows it uses the final handle name, and fails closed
  if that is denied.
- A program that finishes with live reference-counted values exits with code 71. It's a leak check, not a proof of memory safety.

On Linux the runtime uses system calls directly. On Windows it uses Kernel32 only. Programs that use sockets, TLS or SQLite on
Linux also use glibc's loader and threads.

## Building by hand

The installers do all of this for you. To work on the backend from a checkout, build a seed with the reference compiler:

~~~sh
wood build bootstrap/src/native_main.wd -O0 -o build/wood-native
~~~

CMake writes `build/bootstrap-stdlib.json` when you configure. The emitter takes:

~~~text
wood-native STDLIB.json RUNTIME_DIRECTORY TARGET INPUT OUTPUT.ll [--locked]
~~~

Prepare the runtime object once with Clang as a C17 compiler, then build a program:

~~~sh
cmake -DCLANG=/usr/bin/clang -DTARGET=linux-x86_64 \
  -DOUTPUT="$PWD/build/managed-linux.o" -P tools/prepare-native-runtime.cmake

LLVM_DIR=/usr/lib/llvm-19/bin
cmake -DEMITTER="$PWD/build/wood-native" \
  -DSTDLIB="$PWD/build/bootstrap-stdlib.json" \
  -DINPUT="$PWD/tests/native_managed.wd" \
  -DOUTPUT="$PWD/build/native-managed" -DTARGET=linux-x86_64 \
  -DRUNTIME_OBJECT="$PWD/build/managed-linux.o" \
  -DLLC="$LLVM_DIR/llc" -DOPT="$LLVM_DIR/opt" -DLINKER=/usr/bin/ld \
  -DOPTIMIZATION=2 -P tools/build-native.cmake
./build/native-managed
~~~

The output path must not exist yet. Commands, IR, objects and logs are kept under `OUTPUT.stages/`. `OPTIMIZATION=2` runs
`opt -passes=default<O2>` and `llc -O=2`; `0` skips the optimizer. Each stage times out after 180 seconds by default,
adjustable with `-DSTAGE_TIMEOUT`.

On Linux the program is linked with `ld -static -e _start`, with no dynamic loader. On Windows it's linked with the SDK's
`link.exe`, using `/Brepro`, `/nodefaultlib`, `kernel32.lib` and an 8 MiB stack, and may import only from Kernel32.
`TARGET=windows-x86_64` prepares a Windows object.

## Tests

~~~sh
cmake -G Ninja -S tests/native -B build/native-tests \
  -DEMITTER="$PWD/build/wood-native" \
  -DSTDLIB="$PWD/build/bootstrap-stdlib.json" \
  -DRUNTIME_OBJECT="$PWD/build/managed-linux.o" \
  -DLLC="$LLVM_DIR/llc" -DOPT="$LLVM_DIR/opt" \
  -DLINKER=/usr/bin/ld -DPRODUCTION="$PWD/build/wood"
ctest --test-dir build/native-tests --output-on-failure
~~~

The suites run real programs at `-O0` and `-O2` and compare them with the reference compiler:

| Suite | Covers |
| --- | --- |
| `native_arrays` | Numerical results and errors, and the `mesh_import` example's output bytes |
| `native_callables` | Closures, iterators, lifetimes, buffered lines, and the log-report example |
| `native_ffi` | Real C archives, exact diagnostics, reentrancy and exit |
| `native_math` | 7278 binary64 vectors for `sin` and `cos`, and whole-file I/O |
| `native_core_lifecycle` | Failed handles, broken pipes, task errors, HTTPS, SQLite and tasks |
| `native_bootstrap` | Three compiler stages with byte-identical stage 2 and 3. Turn on with `-DENABLE_NATIVE_BOOTSTRAP=ON`; each stage gets 1800 seconds |

With `-DCLANG=/path/to/clang-19`, Linux also runs AddressSanitizer on generated code and AddressSanitizer with UBSan on the
runtime, using a hosted allocator and file port and fault injection. Sanitizers haven't been run on Windows.

On Windows, use PowerShell 7 in a Visual Studio x64 developer shell:

~~~powershell
pwsh -NoProfile -File tests/native/windows.ps1 -Emitter C:/path/to/wood-native.exe -Stdlib C:/path/to/bootstrap-stdlib.json -RuntimeObject C:/path/to/managed-windows.obj -LLVMDirectory /path/to/debian/llvm/bin -WslDistribution Debian -Linker "$env:VCToolsInstallDir/bin/Hostx64/x64/link.exe" -TestDirectory C:/path/to/new-test-directory -SourceDirectory C:/path/to/wood -Production C:/path/to/wood.exe -Cxx "$env:VCToolsInstallDir/bin/Hostx64/x64/cl.exe"
~~~

`-WslDistribution` uses LLVM from WSL; leave it out to use Windows LLVM. Add one of `-BootstrapOnly`, `-CallablesOnly`,
`-ArraysOnly`, `-MathOnly` or `-FfiOnly -NativeLibrary FIXTURE.lib` to run a single suite. Build the FFI fixture from
`tests/native/ffi_fixture.c` as C17.

## Limits

- The very first seed is built by the C++ compiler, and rebuilding the runtime object needs a C17 compiler. The LLVM tools
  are themselves C++ programs.
- Out-of-memory errors carry their location, but aren't guaranteed to match the reference compiler exactly.
- Only Windows and Linux x86-64 are supported. Leaving out libc doesn't make a program portable to other systems.
