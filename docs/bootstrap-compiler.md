# The compiler written in wood

`bootstrap/src/` holds a complete compiler written in wood: project and module loading, the lexer, parser, type checker,
generic specialization and code generation. It has two backends. The installers ship it with the
[LLVM backend](native-backend.md), as the normal `wood` command. This page covers its other backend, which emits C++23, and
is mostly useful for working on the compiler itself. The [`wood-selfhost` driver](selfhost-driver.md) wraps this emitter in
the normal commands.

The C++ emitter covers what the compiler needs to build itself, generic collections and the standard library, including
SQLite, TCP and HTTP, TLS and HTTPS, and tasks. Its output is one C++23 file with wood's checked runtime included, which you
compile with a C++23 compiler. In a source build, `wood` is still the C++ reference compiler in `src/`.

## Build and run

Build the emitter with the reference compiler:

```sh
cmake -S . -B build -DWOOD_PYTHON_TESTS=OFF
cmake --build build --config Release --target wood_bootstrap_compiler --parallel 2
```

It lands in `build/Release/wood_bootstrap_compiler.exe` with Visual Studio, or `build/wood_bootstrap_compiler` with
single-configuration generators. It takes:

```text
wood_bootstrap_compiler STDLIB.json RUNTIME.hpp INPUT OUTPUT.cpp [--locked]
```

CMake writes `build/bootstrap-stdlib.json` and `build/bootstrap-runtime.hpp` from the same standard library and runtime the
reference compiler uses. `INPUT` is a script or a project. `OUTPUT` must not exist yet, and nothing is written if checking
fails. On Linux:

```sh
build/wood_bootstrap_compiler build/bootstrap-stdlib.json \
  build/bootstrap-runtime.hpp tests/bootstrap_compiler_application.wd build/app.cpp
c++ -std=c++23 -O2 build/app.cpp -pthread -ldl -o build/app
build/app
```

With MSVC, compile with `/std:c++latest /O2 /EHsc /utf-8 /permissive- /bigobj`, and link compilers with `/STACK:8388608`.

The exit code is 0 on success, 1 when the program has errors, 2 for usage, an existing output, project configuration or a
lock failure, and 70 for an uncaught runtime error. Errors go to stderr as JSON, with the code, message, file, line and column.

## Locks

The emitter checks a project's `wood.lock` if there is one, and `--locked` requires it. It never writes the lock: create it
with `wood lock PROJECT`. A mismatch reports D5001 and exits with 2.

It compares the compiler version, the manifest bytes, the dependency roots and edges, every dependency `.wd` file (including
modules you don't use) and every standard module, then hashes the dependency sources again after loading, so what it compiles
is what the lock describes. Your own application sources can change freely.

`bootstrap/src/project_main.wd` prints the same inventory as `wood deps`:
`wood-bootstrap-project STDLIB.json INPUT [--locked]`.

Inventories reject filesystem links, and allow at most 4,096 `.wd` files and 65,536 directory entries per dependency, 16 MiB
per file and 128 MiB in total. A lock proves the files match what you reviewed. It doesn't prove who wrote them. See
[`project-trust-contract.json`](../bootstrap/project-trust-contract.json).

## How checking works

`body_semantics.load(input, standards)` loads and binds the modules, resolves signatures, then checks every function, method
and the entry script, and returns a `checked_program.Result`. Each function has its own scope, so script variables aren't
visible inside functions. Branches, handlers and `with` blocks have private declarations, and loop bindings can shadow
outer names.

Calls map each argument to its parameter in source order, with defaults, keywords, positional-only `/` and keyword-only `*`.
A mutating method needs a variable or path as its receiver, and a read-only `self` can't be changed. Return types are inferred
across functions and modules, and an unannotated cycle is rejected with E3030. At most 64 inferences can be in progress at once.

### Generics

Functions, records and methods are specialized for each set of concrete types. Type arguments are deduced through containers,
tuples, optionals, records, function types, `callable` signatures and `.Result` projections. A generic function used as a
value needs a function type annotation.

```wood
record Box[T]:
    value: T

    fn pair[U](self, other: U) -> tuple[T,U]:
        return (self.value,other)

fn identity[T](value: T) -> T:
    return value

number = Box(3)
text = Box("wood")
assert(number.pair("native") == (3,"native"))
f: fn(int)->int = identity
assert(f(7) == 7)
```

Each specialization gets its own checked facts. Generic methods are checked when first used, as in the reference compiler.
A compilation may have at most 64 specializations in progress and 1,024 in total, and E2002 reports going over.
`tests/bootstrap_compiler_generics.wd` exercises generic methods, defaults, keywords and `std.collections`.

The emitted code uses the same checked operations and value semantics as the reference compiler. Reads usually take a
snapshot. String indexing and slicing borrow instead when the index expressions can't have side effects, so scanning a
string doesn't copy it each time. Arguments are evaluated left to right, and mutations re-check their indexes after
evaluating arguments.

### Comprehensions

List and dictionary comprehensions work over lists, arrays, strings, dictionaries and ranges, with nested `for` clauses and
filters:

```wood
squares = [i*i for i in range(10) if i%2==0]
labels = {str(i):i for i in squares}
nested: list[list[int]] = [[] for ignored in range(3)]
```

Each `for` binding has its own scope, and its iterable is evaluated before the binding exists. Iterables are snapshots, keys
come before values, and a duplicate key keeps its first position. Arrays yield their elements in row-major order. A
comprehension may have up to 64 clauses.

`native_calls.wd` lists the runtime primitives this compiler knows, checked against the reference compiler's list. A known
primitive it can't build yet reports B3403. Your own functions with the same name take priority.

### Numerical arrays and C

`array[int]` and `array[float]`, all 16 array primitives and the scalar math primitives are supported, so `std.num`,
`std.linalg` and `std.geometry` work. See [native computing](native-computing.md) for how arrays behave.

```wood
import std.num as num
values = num.reshape(num.from_list([1.0,2.0,3.0,4.0]),(2,2))
view = num.transpose(values)
values[0,0] = 9.0
assert(view[0,0] == 1.0)
assert([x for x in view] == [1.0,3.0,2.0,4.0])
```

`extern fn` takes `int`, `float` and `void` results, and scalars, borrowed byte strings, function pointers and numeric arrays
as parameters. A string becomes `(const char*, int64_t)` and an array `(const T*, int64_t)`, or `(T*, int64_t)` for a `mut`
parameter. Arrays must be contiguous; `num.copy` makes one from a view.

```wood
import std.num as num
extern fn library_scale(mut values: array[float], factor: float)
data = num.from_list([1.0,2.0,3.0])
saved = data
library_scale(data,2.0)
assert(saved[0] == 1.0)
```

This needs a library that exports `library_scale`. The emitter doesn't link libraries, so pass it to the C++ compiler, for
example `c++ -std=c++23 -O2 app.cpp library.a -pthread -ldl -o app`. Callbacks run synchronously on the calling thread. If one
fails, later callbacks in that call are skipped and the error is raised once C returns. Foreign code is trusted: it must not
keep pointers, overrun buffers or throw through C.

### Services and resources

`std.sqlite`, `std.net`, `std.http`, `std.tls`, `std.https` and `std.task` work, with the same `with` handling as the reference
compiler for streams, databases, statements, transactions, TLS contexts and tasks. Task entry points are plain
`fn(json)->json` or `fn(stream,json)->json` functions, not closures. See [backend services](backend.md).

For a sanitizer run of the full bootstrap at `-O0` with GCC or Clang, configure the sanitizer build with
`-DWOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION=0` (the default is `1`), then run
`ctest -R '^bootstrap_compiler$' --output-on-failure --parallel 1`.

### Closures and lazy iteration

`fn(x: int) => expression` and local named functions compile to native structs that own their captures. Only the locals a
closure uses are captured, as immutable copies; `[mut x]` captures a private mutable copy. Parameter types are inferred from
how a closure is called or annotated. Functions at file scope still can't capture script variables, and local functions
can't be recursive or generic.

`callable(T) -> R` is a type-erased function value; `fn(T) -> R` is a plain function pointer. Limits are 255 captures,
64 levels of nested closures and 256 levels of captures containing captures.

`step[T]` separates "no more items" from a present `None`. `iter` makes a cursor, and `next` advances it through
`fn next(mut self) -> step[T]`. Loops take a snapshot, so a cursor keeps its own position.

```wood
import std.iter as it

fn counter(start: int):
    fn advance[mut start]() -> int:
        start += 1
        return start
    return advance

state = counter(10)
assert(state() == 11)
copy = state
assert(copy() == 12 and state() == 12)
pipeline = it.take(it.map(range(100), fn(value) => value * 3), 4)
assert(it.collect(pipeline) == [0,3,6,9])
```

See [closures and iterators](closures-iterators.md) for the language rules. One oddity carries over from the reference
compiler: a `for` loop over a record whose `iter` returns a bare range is rejected with E3010, while `iter(record)` called
directly accepts it.

## The bootstrap test

```sh
cmake --build build --config Release --target wood \
  wood_bootstrap_compiler_oracle wood_bootstrap_semantics_oracle \
  wood_bootstrap_lexer_oracle wood_bootstrap_parser_oracle \
  wood_ffi_fixture wood_obj_check --parallel 2
ctest --test-dir build -C Release -R '^bootstrap_compiler$' --output-on-failure
```

1. The reference compiler emits a seed compiler, which the C++ compiler builds.
2. The seed emits stage 1, stage 1 emits stage 2, and stage 2 emits stage 3. All three emitted sources must be byte-identical.
3. Stage 2 builds the semantic tests, real applications, and the lexer and parser.
4. The test compares diagnostics, inferred types, program output and the lexer and parser corpora with the reference compiler.

The test compares emitted sources rather than executables, because C++ compilers can embed paths and timestamps.
[`compiler-contract.json`](../bootstrap/compiler-contract.json) summarizes what this backend supports.
