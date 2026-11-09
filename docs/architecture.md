# Compiler architecture

wood has two compilers, both held to the same [language reference](language.md):

- **Native `wood`**, which the installers ship, is written in wood in `bootstrap/src`. It emits LLVM IR, which `opt`, `llc`
  and a linker turn into an executable, linked with a small C17 runtime object (`runtime/native`, ABI 4). Ordinary programs
  contain no C++ runtime and no C library. It rebuilds itself in [three stages](native-backend.md) that must reproduce the
  same bytes, and anything its backend can't build stops with an error rather than falling back to C++.
- **Reference `wood`**, which a source build produces, is in `src` and `include`. It's standard C++23 with no third-party
  libraries, emits C++23 and leaves optimization and linking to the host compiler. It stays independent of the other, as
  a test oracle and the most complete implementation of the language.

A third combination, [`wood-selfhost`](selfhost-driver.md), pairs the frontend and driver written in wood with a C++
backend. It's for comparing the two and isn't distributed. See the [support matrix](support.md).

Most of this page describes the reference compiler:

```text
source -> lexer -> tokens -> parser -> owned AST -> semantic analysis
                                                     |
                                                     v
                  executable <- native compiler <- C++ backend + runtime
```

## Components

| Component | Responsibility |
| --- | --- |
| `include/wood/compiler.hpp` | Source locations, diagnostics, tokens, AST, the typed frontend API |
| `src/lexer.cpp` | Indentation, logical lines, decoding literals, lexical errors |
| `src/parser.cpp` | Recursive-descent statements and precedence-climbing expressions |
| `src/semantic.cpp` | Signatures, name resolution, scopes, type checks, control-flow exits |
| `src/codegen.cpp` | Typed AST to deterministic C++ |
| `src/modules.cpp` | The module graph, exports, package-scoped resolution, import errors |
| `src/project.cpp` | Manifests, lock inventories and verification, creating and inspecting projects |
| `src/io.cpp`, `include/wood/filesystem.hpp`, `include/wood/json.hpp`, `src/hash.cpp` | Filesystem and canonical paths, strict JSON, SHA-256 |
| `runtime/wood_runtime.hpp` | Checked integers, range cursors, conversions, output, runtime errors |
| `runtime/wood_streams.hpp` | Native resources, scoped cleanup, buffered file and TCP I/O, socket deadlines |
| `src/main.cpp` | The CLI, choosing the C++ compiler, temporary files, publishing output |
| `src/process.cpp` | Starting processes without a shell, and exit statuses |

## Semantic analysis

AST nodes own their children through `unique_ptr`. Analysis annotates every expression with a type and every reference with
a unique symbol ID. Signatures are collected before bodies, so functions can call each other in any order. Functions don't
see script variables. Each block tracks how it can exit (`next`, `return`, `raise`, `break`, `continue`), which gives the
all-paths return check and unreachable-code errors; loops always keep a possible fall-through.

Types are structural descriptors. Context resolves the type of empty and nested list literals at declarations, assignments,
parameters, returns, comparisons and list mutations. Named list types are invariant, and type nesting has a depth limit.
Built-in names are registered once in `include/wood/builtins.hpp`, but their type rules are written out in the analyzer,
not left to C++.

Generic functions share a specialization context for the compilation. A call infers the type arguments, caches a copy of
the function's AST before checking its body (so recursion works), and checks that copy in its defining module. The backend
sees only concrete instances. Limits bound type depth, instantiations in progress and the total. Instances share their
module's read-only declaration tables rather than copying them.

Generic records work the same way: a concrete instance is cached before its fields are resolved, so recursive list and
dictionary fields can refer back to it, and containment is checked afterwards. Method signatures are prepared before
bodies, and a worklist checks methods without deep recursion. `self` is a value with a write restriction; `mut self` is a
native reference for the duration of the call only. Dispatch is static: there are no vtables or runtime type dictionaries.
See [generic records](generic-records.md).

Calls keep the order arguments were written separately from how they bind to parameters, so the backend evaluates them in
source order and then calls in parameter order. Defaults are literal, independent values. Function pointers take all their
arguments positionally.

## Code generation

The backend only ever receives a checked, typed program. wood names never become C++ identifiers: symbol IDs do, so there
are no clashes with C++ keywords or generated temporaries. Strings and file names are written with fixed-width octal
escapes, including NUL, and user source is never pasted into C++. Numeric literals are validated and normalized first.

Immediately invoked lambdas and temporaries keep binary operands and arguments evaluated left to right, and C++'s
short-circuit operators implement `and` and `or`. Range loops use a runtime cursor separate from the loop variable.
Nothing backend-specific may leak into the documented semantics.

Ordering matters wherever an expression has effects:

- Indexed reads copy the list handle before evaluating the index.
- An indexed assignment evaluates the right side first. A mutating call records its target indexes, evaluates the other
  arguments, and only then resolves the path, so no element reference survives something that could move the storage.
- Augmented assignment snapshots the read and its indexes before the right side, then checks the path again.
- Tuple elements are copied in order, because `std::tuple`'s constructor could otherwise copy a referenced field after a
  later expression changed it.
- A read-only string argument can borrow a stable local or literal; other strings are copied before later arguments run.

Comparison chains store each operand once and lower to short-circuit checks. Comprehension clauses are checked before their
result expression, so bindings stay private, and comprehensions share their lowering with `for`. Builtin checks and
expression emission use small helpers rather than one huge recursive frame, which keeps deep nesting within the stack.

## Runtime and lifetimes

The runtime is embedded in the compiler when CMake configures, so an installed compiler doesn't need the checkout's headers.
Streams, optional library loaders, SQLite, TLS, tasks and resource guards are embedded too, and SQLite and OpenSSL load on
first use. wood modules implement row conversion, HTTP and HTTPS policy and the public wrappers; C++ handles the checked ABI
calls, threads, OS transport and destruction.

Locals and temporaries use ordinary C++ lifetimes. Lists wrap shared vectors that copy on first write; `bool` elements are
stored as real values, avoiding `vector<bool>`. Empty lists don't allocate, nested copies share nested storage, and
finite structural types mean no list can contain itself. Strings are `std::string`, so passing a named string can copy it.
There's no garbage collector, JIT or interpreter.

`with` lowers to a scope guard that reports close errors on normal exit but never replaces an exception already unwinding.
`stream` values share identity, unlike the copy-on-write containers. Tasks exchange immutable JSON and move socket state, so
threads never share mutable wood data, and the owning task joins before it's destroyed. C callbacks use a thread-local stack
to hold a C++ exception until foreign code returns. See [backend services](backend.md).

Dictionaries keep entries in insertion order with a balanced-tree index; writes detach shared storage, and iteration
snapshots the keys. Records are native structs whose container fields keep their own copy-on-write rules. Equality and JSON
conversion are generated after all records are declared. JSON values are immutable shared nodes.

`try` catches only `wd::Error`, and the handler receives an immutable copy; raising it again keeps the original location.
`exit` uses a separate unwinding signal caught only at the entry point. A stray C++ exception reaches the entry point as R4099.

On Windows, MSVC links Winsock through a pragma, and the CLI adds `-lws2_32` for GNU-style compilers. On POSIX, generated
programs use `-pthread`, plus `-ldl` on Linux. Entry points convert Windows wide arguments to UTF-8.

Float literals are validated with a classic-locale stream, because libc++ added floating-point `from_chars` late
([LLVM's table](https://libcxx.llvm.org/Status/Cxx17.html)).

The backend prefers simple, debuggable code to clever optimization. Integer checks are visible to the optimizer, which can
remove the ones it proves redundant. Measure before assuming a copy or a lambda costs something.

## Closures and iterators

A closure owns its function's AST. A nested analyzer resolves outer names, records captures by symbol, including transitive
ones, and keeps them immutable unless declared `mut`. Local functions become closure-valued assignments. Closures become C++
structs, emitted after records in dependency order, with their call operators after the function prototypes. A pure closure
can be borrowed for a call when later arguments can't change it; otherwise it's copied.

`callable(...) -> R` uses the runtime's copyable type-erased wrapper, with small targets stored inline. Plain `fn` values
stay raw function pointers. A callback whose type comes from a later argument can be checked after the others, while the
generated code still evaluates them in written order.

Iterators are checked for `iter(self)` and `next(mut self) -> step[T]`, and loops over them lower to an owned snapshot, direct
`next` calls and a presence test. Built-in cursors live in `runtime/wood_callables.hpp`, and the adapters in `stdlib/iter.wd`.
Iteration yields values: there are no borrowed items and no coroutine state machines.

## Modules and projects

The loader owns every module's source and AST until code generation finishes, and locations carry a module ID. Paths are
resolved before identity and containment checks. Windows compares paths case-insensitively for ASCII, and if a managed
filesystem denies the final-path API, the resolver follows each link itself rather than trusting the text of the path. That
resolver, in `include/wood/filesystem.hpp`, is shared by the compiler and the runtime's path functions.

Every function gets a graph-wide ID before any module is analyzed. Each module sees its own functions and the public
functions it imports explicitly. Imports run no code, and cycles are rejected with the import chain. Only the entry module
may have top-level statements. `std.*` modules go through the same frontend.

The project layer validates strict JSON manifests, walks the graph of relative local packages, and hashes manifests and
dependency sources. Verifying a lock compares the inventory, then checks the dependency text the compiler actually loaded.
Root sources can change freely. Locks cover the standard sources and compiler version, but not native compilers or options,
so they don't make native builds reproducible. See [projects](projects.md).

## The driver

C++ compiler arguments are passed as vectors to `CreateProcessW` or `fork`/`execvp`, never through a shell, and Windows
arguments are quoted by its native rules. Compiler logs are kept apart from program output, and captured output is limited
before it's written. Deadlines and cancellation are checked while draining output. On Windows, children run in a kill-on-close
job; on POSIX, in their own process group.

Each build uses a fresh directory with random components, and output is staged beside its destination and renamed into place,
so a failure leaves the previous file intact. A crash or a killed process can leave directories behind. Process launches are
serialized and cancellation handlers are process-wide, so this isn't a general subprocess library.

## Adding a feature

Change the lexer and parser, check it in semantic analysis, implement its runtime behavior in code generation and the runtime,
and add tests that it works and that misuse fails. Document it before calling it supported, and never implement a feature as
an unchecked native fragment. Plans are in [the roadmap](roadmap.md).

## Pitfalls

These have caused real bugs. Read [data-model.md](data-model.md), [closures-iterators.md](closures-iterators.md) and
[native-computing.md](native-computing.md) before changing ownership.

- Don't keep a pointer into the scope maps across analysis that can grow the scope vector. Copy what you need first.
- Typed facts belong to each generic instance. Never share mutable facts between instances; comprehension facts use the
  cloned expression's ID.
- `facts.types`, `symbols`, `annotations` and `statements` are lists indexed by AST node. Check the index and the negative
  sentinel: `index in list` tests values, not presence. A `-1` arena link means absent.
- Parser type indexes, semantic type IDs, declaration IDs and instance symbols are different kinds of identity. Resolve
  nominal types by module and declaration, not display name, and look symbols up rather than computing them.
- Closure declarations use the internal parent `-2`. Capture lookup follows the saved lexical frames, not the inference call
  stack, and intermediate closures must forward their children's captures.
- Methods and mutating calls validate their target before the arguments and reacquire it afterwards. Never hold an element
  reference across argument evaluation; `tests/native_runtime_values.wd` has the cases where an argument replaces the
  target's storage.
- A string or array read may borrow only while the owner is pinned and later indexes can't change it. Keep that list of
  safe expressions conservative. A C++ `std::get` reference doesn't extend a temporary tuple's life.
- A read-only `for` over a list of strings borrows from a hidden snapshot only if the loop variable is never written,
  including through `mut` captures.
- Moving a value out on its final return excludes captures, closures, comprehensions, mutable foreign borrows and local
  handlers. Move marks are per module; reset them per function and before module initialization.
- C callbacks must never unwind through C. Task entry points are raw `fn` pointers, not closures.
- `with` keeps the checked handle's real type and closes the handle it entered on every exit, even if the name changes.
- Never silently send an unsupported LLVM feature to C++: raise B3501. Keep the emitter, runtime IR and runtime object at the
  same ABI, and run the staged bootstrap after any change to what the compiler emits.
- Never read or write a native owner's reference count non-atomically. A task must be joined before its stack, context,
  callbacks or results are freed.
