# Native borrowed C ABI

wood can call separately compiled native static libraries through explicit,
bodyless `extern fn` declarations. This is a narrow real interface, not a general
C header importer. It requires no Python, interpreter, package service or bridge.

Native computing also supports contiguous `array[int]` / `array[float]` input
buffers and `mut name: array[T]` external parameters. They expand to a numeric
pointer and int64 flat length, with synchronous lifetime and copy-on-write
detachment for mutable callers. See [native computing](native-computing.md#native-numeric-buffer-interoperability)
for complete layout, ownership, aliasing and trusted-native-code contracts.

The C side must use C linkage and the exact signature:

```cpp
#include <cstdint>
extern "C" std::int64_t wood_native_add(std::int64_t left, std::int64_t right) {
    // The caller must keep this particular example's sum in range.
    return left + right;
}
```

The wood side is:

```wood
extern fn wood_native_add(left: int, right: int) -> int
assert(wood_native_add(20, 22) == 42)
```

Compile the native implementation with a compatible target toolchain into a `.a`
or `.lib` static archive. Then run `wood program.wd --link-library /absolute/path/to/archive.a`,
or use `wood build ... -o app --link-library ...`. Repeat the option for multiple
archives; order is preserved for toolchains with order-sensitive static linking.
wood's MSVC drivers disable identical-function folding with `/OPT:NOICF` because
distinct declared functions have distinct identities. Use that linker option
when manually building emitted C++ as well.
Relative input archive paths are resolved against the command's working directory
and converted to absolute paths before launching compiler/test children. Spaces
are supported; arguments never pass through a shell. Only existing regular `.a`
and `.lib` files are accepted, not compiler flags or library search names.
Archive extensions are recognized ASCII case-insensitively, including canonical
Windows SDK names such as `User32.Lib`; this does not change filesystem lookup,
path identity or the regular-file requirement.

`wood test PROJECT --link-library PATH` forwards each archive to native test
compilations. `check`/`inspect` do not link archives, and reject this option.
`emit-cpp` emits the declarations/wrappers, while manually compiling its output
requires supplying the archives yourself. A failed link reports D5002 and does
not replace a previous build output. `wood inspect` adds `external: true` to the
function export; regular wood functions carry `external: false`.

## Exact contract

| wood type | C ABI type |
| --- | --- |
| `int` | `int64_t`, signed exactly 64 bits |
| `float` | `double`, IEEE binary64 |
| `void` | `void`, return only |
| `str` parameter | `const char* data, int64_t byte_length`, immutable borrow |
| `fn(int/float, ...) -> int/float/void` parameter | Synchronous borrowed C function pointer |
| `array[int]` / `array[float]` parameter | Const numeric pointer plus int64 flat element count; contiguous borrow |
| `mut name: array[T]` parameter | Writable numeric pointer plus int64 count; rooted argument, COW detachment |

No C int/long/bool/size_t inference, raw pointers, string returns, structs, variadics,
retained callbacks, general shared-library loading, symbol renaming or ownership transfer is
implemented. Use a small native adapter with exact types around another library
when possible. Byte strings preserve embedded NUL, have no promised terminator,
and must not be retained, modified or freed. Callbacks must run on the calling
thread before the foreign call returns. wood saves the first callback failure,
returns a zero/void fallback to C, skips later invocations after failure, then
rethrows in wood. Reentrant calls restore their previous callback target.
Foreign side effects remain. See [backend.md](backend.md#borrowed-c-abi).

Archives and ABI declarations are trusted native code. Matching names do not
prove compatible signatures, architecture, calling convention, runtime-library
settings, dependencies or semantics. Wrong signatures can cause undefined
behavior. Foreign functions must not throw across the ABI. They can terminate
the process, modify global state or perform I/O; wood's runtime checks do not
cover foreign implementation behavior. Select names compatible with headers
already included by the generated C++ runtime; incompatible C declarations are
reported by the host compiler. Avoid collisions with process entry points and
reserved toolchain symbols. There is no native dependency resolver.

Native archives are explicit command inputs and are **not included in wood.lock**.
Record exact target/toolchain/archive hashes in the application's build/release
manifest. Source/dependency locks alone are not reproducible native builds.

## Experimental LLVM path

The development [native profile](native-backend.md) implements these same scalar,
byte-string, callback and numeric-buffer contracts. External values point to
wood wrappers; the wrappers flatten borrowed arguments and supply separate raw
C callback trampolines. Callback targets are dynamically scoped per native worker
and restored on reentrant calls. Independent workers can call the same foreign
wrapper concurrently without sharing its targets or checked context. Retained
callbacks and callbacks on a different thread from the foreign caller remain
unsupported. A checked callback failure never
unwinds through C; later callbacks are skipped and the original error is
propagated after the foreign call returns. Foreign side effects remain.

The [normal LLVM CLI](native-cli.md) accepts repeatable `--link-library` paths.
For its positional-entry CMake build helper, pass `-DLINK_LIBRARIES=/absolute/path/library.a`
(`.lib` on Windows); a CMake list supplies multiple archives in order. Only
existing `.a`/`.lib` files are accepted, not linker options or search names.
Prepare archives with the exact target ABI and review their dependencies. The
freestanding FFI test archives require no CRT; arbitrary user libraries may have
their own dependencies. Missing symbols fail at link time without publishing a
partial executable. A successful link says nothing about whether a library is safe.
In particular, ordinary Linux workers are freestanding clone/futex threads and
do not initialize hosted pthread/libc TLS. Libraries requiring that startup or
their own runtime or threads aren't covered by the freestanding fixture tests, and the pthread-based sanitizer build
doesn't show they work in normal native executables either.

## Tested integration

The CMake test target `wood_ffi_fixture` builds
[`tests/ffi_fixture.cpp`](../tests/ffi_fixture.cpp), whose saturating integer
addition checks its own overflow, and whose interpolation takes three doubles.
[`examples/ffi.wd`](../examples/ffi.wd) exercises signed extremes, numeric argument
widening, void return, byte borrows, callbacks, reentrancy, deferred callback errors
and an external function value. The `native_ecosystem`
CTest suite runs it at `-O0`/`-O2`, checks absent-library failure/output preservation,
and verifies archive propagation through `wood test`.
