# The compiler written in wood

This directory holds the compiler, CLI, package manager and language server that the installers ship as `wood`. Below,
"production compiler" or "default backend" means the C++ reference compiler in a source build.

- The [LLVM backend](../docs/native-backend.md) builds this compiler and ordinary programs without any C++, and the compiler
  rebuilds itself through it with stage 2 and stage 3 identical. [native-backend-contract.json](native-backend-contract.json)
  says what that backend supports. The [native CLI](../docs/native-cli.md) wraps it in the normal commands.
- The [C++ emitter](../docs/bootstrap-compiler.md) covers the compiler itself, generics, comprehensions, closures and
  iterators, arrays and C calls, SQLite, networking, TLS and tasks. [compiler-contract.json](compiler-contract.json) is its
  summary.
- [`wood-selfhost`](../docs/selfhost-driver.md) wraps that emitter in the normal project, build and test commands, with a
  small native archive for OS access. See [driver-contract.json](driver-contract.json).
- The compiler verifies `wood.lock` files, with optional `--locked`, and hashes the dependency bytes it actually loads.
  `src/project_main.wd` prints the same inventory as `wood deps`. See [project-trust-contract.json](project-trust-contract.json).
  The older inspection tools below still report `lock_verified=false`.

The rest of this page describes the individual frontend stages, each of which can be built and tested on its own.

These are real, separately testable **hosted lexer, parser, project/module loader
and declaration/import binding, signature resolution, declaration validation,
core expression typing and semantic preparation
stages written in wood**, with owned syntax and
module representations. The existing C++ compiler remains the production compiler.
The older inspection tools alone do not compile source; use the integrated
compiler above for the self-compiling hosted profile. Full wood 0.4 coverage
through LLVM, complete host-toolchain independence and general freestanding
targets remain future work; the implemented native profile is described above.

## Run

From the repository root with the current wood executable on PATH and a configured
C++23 toolchain (use a Visual Studio developer shell for MSVC):

```sh
wood check bootstrap
wood test bootstrap --report=json
wood build bootstrap -o build/wood-bootstrap-lex
./build/wood-bootstrap-lex bootstrap/src/lexer.wd
wood build bootstrap/src/parse_main.wd -o build/wood-bootstrap-parse
./build/wood-bootstrap-parse bootstrap/src/parser.wd
```

Use an `.exe` output suffix on Windows. These commands run the native lexer/parser
on their own source. They do **not** compile it or reproduce their executables.
Normal bootstrap builds and tests require no Python or third-party library.

## Library contract

`src/lexer.wd` exports `lex(source, max_bytes=16777216, max_tokens=262144)`.
`Result` contains `tokens`, `code`, `message`, `offset`, `line`, and `column`.
An empty code means success; failure returns an empty token list and a precise
position/message. Tokens have kind, decoded/raw text, byte offset, line and column.
`Scanner` and helper records/functions are implementation details; only `lex`,
`Result`, and `Token` are intended for consumers. wood does not enforce privacy here.

The lexer handles the complete current lexical vocabulary: 61 token kinds,
keywords and contextual identifiers, ASCII identifier rules, decimal/exponent
spellings, escaped single/double strings, comments, indentation/dedentation,
logical newlines, nested delimiters, longest-match operators and augmented stores.
Strings/comments can contain arbitrary bytes. It deliberately does not convert
numeric literals to values; range checking belongs to parsing/semantic analysis.

Positions follow the existing lexer: zero-based byte offsets, one-based physical
lines and byte columns; CRLF is one newline, and an initial UTF-8 BOM counts only
toward the byte offset. Blank/comment lines do not change indentation. Delimiters
suppress logical newlines and indentation. EOF emits a pending logical newline,
remaining dedents and one Eof token. Lexical errors retain E1001–E1007 semantics.

Input is bounded to at most 16 MiB; callers may lower that budget. Token budgets
count synthetic tokens too, are in 1..1,048,576, and default to 262,144. B1001/B1002
are **bootstrap result codes**, not new production compiler diagnostic codes.
Indentation remains limited to 255 blocks and delimiters to 256 levels. The extra
token quota and Result-returning API differ from the production C++ lexer; this
is not a drop-in replacement. Quota compatibility needs review before integration. Allocation
failure and hosted I/O errors still follow the existing runtime; these budgets
are not a hard OS memory quota. Input strings and returned tokens retain wood value
semantics. Scanning is iterative; ordinary string runs are sliced and escape chunks
are joined once instead of repeatedly concatenating growing decoded strings.

The core imports no filesystem, process, FFI, JSON, or OS library. The native CLI
adapter alone reads bounded binary streams and prints one JSON object per source
file. Token payloads use `text_hex`, preserving even invalid UTF-8 and NUL bytes.
Exit status is 0 for all successful scans, 1 for any lexical failure, 2 for missing
arguments, or the existing runtime failure status for I/O. Large JSON reports can
exceed the compiler driver's default output-capture budget; use the standalone
executable or an explicitly increased capture budget. No output file is overwritten.
The versioned machine-readable contract is [lexical-contract.json](lexical-contract.json).

## Verification and boundaries

`bootstrap_lexer` in CTest compiles this project, runs native wood unit checks,
and compares the native program with an independent adapter around `wood::lex`.
It checks **exact** token kinds, decoded bytes, offsets/lines/columns, EOF,
error codes/messages/positions and exit status. The corpus includes all 256 byte
values inside and outside strings, malformed syntax, nesting boundaries,
fixed-seed mutations, repository sources, and this lexer/driver/test themselves.
The oracle and fixture generator are testing tools; the wood lexer never calls them.

The current generated runtime is hosted C++23. Absence of OS imports in these
components is a useful boundary, not evidence of bare-metal execution. The next
stage is expression/body type checking and generic specialization over the owned AST.
Self-compilation requires the complete pipeline and repeatable
stage-to-stage artifact checks. Independent machine-code generation, target ABIs,
linking, explicit low-level memory/layout control, and a freestanding runtime remain
separate, unimplemented work. No speed or universal-platform claim is made here.

## Parser and owned AST

`src/parser.wd` exports `parse(source, max_nodes=262144)`. An empty `code` means
syntactic success; it does **not** establish that names, types, control flow or
imports are valid. On failure, `code/message/offset/line/column` describe the first
lexical or parser error, and all returned tree arenas/roots are empty.

```wood
import parser as parser

result = parser.parse("fn identity[T](value: T) -> T:\n  return value\n")
assert(result.code == "")
declaration = result.tree.statements[result.tree.roots[0]]
assert(declaration.kind == "Function")
assert(declaration.type_parameters == ["T"])
assert(declaration.parameters[0].name == "value")
```

The parser implements the current C++ parser's grammar: functions and aliases,
generic records/methods, mutable receivers/captures, local functions, closures,
defaults and parameter modes, imports, resource scopes, exceptions, control flow,
assignments, precedence/comparison chains, collections, comprehensions, indexing,
slices, optional/tuple/function/callable/nominal type annotations and generic calls.
`None` remains a contextual name until semantic analysis, as in the C++ parser.
Function return markers indicate syntactic inference requirements; this component
does not perform return-type inference or generic specialization.

`src/syntax.wd` defines `Tree(types, expressions, statements, roots)`, typed records
for each arena, parameters/imports/comprehension clauses, and the result record.
Children, declaration links and types are checked integer indices into their own
arena. **-1 means absent: check it before indexing**, since ordinary wood negative
indexing is valid. Type indices 0/1 are Unknown/Void; all other allocation order is
an implementation detail. Locations use the lexer's byte coordinates; the caller
owns source-file identity. Type nodes are syntax, not interned semantic types;
different indices may represent structurally equal annotations. There are no token
end spans or multi-source loader yet.
Trees retain snapshot/value semantics and can be consumed without calling C++.
The parser and AST core import no OS, FFI, I/O or JSON libraries.

Ambiguous `name[types](...)` retains both explicit types and an optional indexed
call interpretation for later semantic resolution. Failed probes restore cursor,
nesting counters, diagnostics and unused arena entries. They do not hide unexpected
runtime failures. Node creation is bounded before allocation; rejected probes
count against the transient budget and pop their discarded entries.

The node budget includes all three arenas and the initial two type nodes. Allowed
budgets are 2..1,048,576. B2001 rejects invalid budgets; B2002 rejects exhausted ones.
These are bootstrap-only result codes, not production diagnostic changes. Lexer's
16 MiB source/default token budgets also apply. Existing parser-specific structural
limits and E2001/E2002/E2003/E2004/E2006/E2010/E2011/E2012/E3020/E3023/E3027/E3028
are retained. Counts bound logical work, not exact allocated bytes or OS memory.

Recursive parsing also needs a host stack budget. The Windows ASan gate emits C++
and links this frontend with `/STACK:8388608`, matching the C++ frontend's 8 MiB
reservation. Its default 1 MiB instrumented executable reproduced a stack overflow
at 255 nested blocks. Ordinary Release builds pass that case with the default
stack. Use the CTest sanitizer gate or the documented emitted-C++ approach with
that linker option when instrumenting this component; this is a per-executable
setting, not a global system change or a reduced language nesting limit.

`src/syntax_wire.wd` iteratively projects reachable syntax into ordered prefix
events. It preserves every parser-owned AST field, optional child, list boundary,
parameter mode, capture, import alias, type structure and source position. All value
bytes are hex encoded, so NUL/invalid UTF-8 cannot disappear in JSON conversion.
Unreachable arena entries, internal depth bookkeeping and semantic-only fields are
excluded. Its input must be a parser-produced tree whose index/acyclicity invariants
have been preserved; it is not a validator for arbitrarily edited graphs.

The CLI returns one JSON object per file; statuses are 0 for all syntactic successes,
1 for any frontend failure, 2 for missing arguments, and the existing runtime I/O
failure status for missing/unreadable files. Resource scopes close inputs before
parsing. The versioned schema is [syntactic-contract.json](syntactic-contract.json).
The adapter is tooling/development source, not a new normal `wood` CLI command.

`bootstrap_parser` in CTest builds and runs native wood, checks its unit tests and
contract vocabulary, and compares exact AST events or error code/message/position
and exit status with `wood::parse(wood::lex(source))`. It includes every emitted
expression/statement kind, all parser error codes above, successful/rejected limit
pairs, fixed-seed mutations/generated expressions, byte fixtures, stdlib, examples,
module/package examples and the parser/AST/driver/tests themselves.

```sh
cmake --build build --config Release --target wood wood_bootstrap_parser_oracle wood_bootstrap_lexer_oracle
ctest --test-dir build -C Release -R "^bootstrap_(lexer|parser)$" --output-on-failure
wood build bootstrap/src/measure_main.wd -o build/wood-bootstrap-measure
./build/wood-bootstrap-measure 100 bootstrap/src/parser.wd
./build/Release/wood_bootstrap_parser_oracle --measure 100 bootstrap/src/parser.wd
```

For single-config Linux, omit `--config Release` / `-C Release` and the `Release/`
binary subdirectory. The optional measurement tools time lex+parse+tree destruction;
file I/O, warmup and JSON serialization are outside the timed loop. They report
iterations, source bytes, root-count checksum and elapsed nanoseconds. Always record
toolchains/flags and run repeated samples. These are frontend measurements, not
generated-application performance or independent-backend evidence.

## Project modules and declaration binding

`src/frontend.wd` exports `load(input: str, standards: dict[str,str]) -> Result`.
It discovers an existing format-1 `wood.json` for a project, manifest or source
path, resolves relative local dependencies with exact versions, loads the owned
module graph and binds declarations and imports. Standalone `.wd` files use their
parent as the source root. `standards` maps `std.name` to source bytes; CMake
generates `build/bootstrap-stdlib.json` from the same sources embedded by the
production compiler. Normal execution never invokes the C++ reference oracle.

On success, `Result.packages`, `.compilation` and `.bindings` hold owned values.
On failure, `.diagnostic` identifies the first failure and all partial results are
discarded. Its fields are code, raw byte message, file, source ID, byte offset,
line and column. Driver/configuration failures use D5001 and zero coordinates.
Source IDs are module indices; declaration symbols are stable, graph-wide,
one-based IDs. AST arena links remain private to each module.
On failure the source ID refers to the discarded graph; use the diagnostic file
and coordinates, never index the empty failure graph. Unlike the production C++
diagnostic, this stage does not retain a separate source-text snapshot on failure.

```wood
import frontend as frontend

# Supply this from a verified bundle if the project imports standard modules.
result = frontend.load("example/main.wd", {})
if result.diagnostic.code == "":
    print(len(result.compilation.modules), len(result.bindings.symbols))
else:
    print(result.diagnostic.code, result.diagnostic.message)
```

The binding stage handles global functions/records, private exports, namespace
aliases, named imports, declaration IDs and collision/reserved-name diagnostics.
It does **not** check signatures, fields/methods, expression names, types, generic
instantiation, inference, captures or control flow. Import success can therefore
coexist with invalid function bodies. It also does **not** verify/write locks or
dependency source snapshots. Use the production `wood check` and locked project
commands for those guarantees. Inspection JSON states `types_checked=false` and
`lock_verified=false` explicitly.

```sh
wood build bootstrap/src/module_main.wd -o build/wood-bootstrap-modules
./build/wood-bootstrap-modules build/bootstrap-stdlib.json bootstrap/src/module_main.wd
cmake --build build --config Release --target wood wood_bootstrap_modules_oracle
ctest --test-dir build -C Release -R '^bootstrap_modules$' --output-on-failure
```

Use `.exe` on Windows. The module CLI accepts multiple entries, emits one
format-1 JSON object per entry and returns 0 for all successes, 1 for any frontend
failure, 2 for missing arguments, or the runtime failure status for invalid bundle
I/O/schema. Sources and diagnostic messages use lowercase hex bytes; paths are
JSON strings. This preserves even locale-encoded Windows filesystem messages.
Bindings reference symbol IDs; imports and symbols use top-level root ordinals,
not AST arena allocation indices. No output file is written by inspection.

The recursive frontend validation executable uses an explicit 8 MiB MSVC stack
reserve, matching the C++ frontend and allowing sanitizer frame overhead. To
reproduce that instrumented build, use `wood emit-cpp` and compile with
`cl /std:c++latest /O2 /EHsc /utf-8 /fsanitize=address modules.cpp /link /STACK:8388608`
in a developer shell. Normal generated applications retain their host stack
defaults; arbitrary small stacks are not supported by a resource quota alone.

Budgets are 128 packages, 1 MiB per manifest, 1,024 modules, 128 import levels,
16 MiB per source and 128 MiB aggregate source. Parser/lexer limits still apply.
`modules.load(..., limits=modules.Limits(...))` may lower graph/depth/byte budgets;
the node budget accepts the parser's documented range. B3101 rejects invalid
arguments and is bootstrap-only. Limits are not an OS memory quota. Canonical
paths deduplicate modules and reject source-root escapes, including directory
links. Only the root package's tests directory adds an allowed source root.
Filesystem checks do not eliminate concurrent path-replacement races.

`project.resolve`, `modules.load` and `bindings.bind` are independently callable
stages. `bindings.bind` consumes a loader-produced graph with preserved AST/edge
invariants; it does not validate an arbitrarily corrupted hand-edited graph.
The public result values retain ordinary wood snapshot/value semantics.

The versioned [module contract](module-contract.json) records the exact boundary.
Timing drivers measure full manifest/module loading, lexing, parsing, binding and
destruction, including warm-cache filesystem reads but excluding bundle decoding
and graph JSON output:

```sh
wood build bootstrap/src/module_measure_main.wd -o build/wood-bootstrap-module-measure
./build/wood-bootstrap-module-measure build/bootstrap-stdlib.json 10 bootstrap/src/module_main.wd
./build/Release/wood_bootstrap_modules_oracle --measure 10 bootstrap/src/module_main.wd
```

## Semantic types, signatures and declaration checks

`signature_frontend.load(input, standards)` extends `frontend.load` with semantic
signature resolution and unspecialized declaration checks. It does **not** change
the existing module-only API or its JSON schema. Use the new standalone tool:

```sh
wood build bootstrap/src/signature_main.wd -o build/wood-bootstrap-signatures
./build/wood-bootstrap-signatures build/bootstrap-stdlib.json bootstrap/src/signature_main.wd
cmake --build build --config Release --target wood wood_bootstrap_signatures_oracle wood_bootstrap_modules_oracle
ctest --test-dir build -C Release -R '^bootstrap_signatures$' --output-on-failure
```

Use `.exe` on Windows and a developer shell for MSVC. The native test gate uses
an explicit 8 MiB MSVC frontend stack; Linux uses its normal host stack. Both
the signature tool and native units/projection tests receive ASan+UBSan in the
Linux sanitizer profile and ASan on Windows. No Python is needed for the gate.

`signatures.resolve(compilation, bindings, max_types=1048576)` returns an owned
`Result(compilation, pool, declarations, diagnostic)`. Resolved annotations live
in `pool`; parser type-node indices stay in the syntax tree and are not semantic
identities. Each declaration records its module/statement, global symbol, parent,
resolved return type and parameters. Parameters retain names, modes, mutable
borrowing, defaults and record field ordinals. Methods follow global declarations;
their symbol is zero until the later body-analysis symbol allocation stage.
The new semantic arena uses checked integer links and interns each structural
type using kind/name/nominal declaration plus immediate child IDs. Record aliases
share nominal identity; different records with equal display names do not.
Internal pool allocation order is not a serialization ABI.

`type_system.Pool` supplies interning, substitution, abstract-parameter detection,
assignment compatibility, structural depth checks and readable type names.
Compatibility preserves invariant function/collection types, int-to-float widening,
optional payload/None rules and callable erasure rules. These utilities do not
themselves perform expression analysis. Type parameters preserve the existing
compiler's name-based identity and declaration-local binding context.

The resolver implements generic parameters and their reserved/duplicate rules,
qualified nominal types and arities, native handles, tuples/optionals/collections,
callable patterns, `.Result`/`.Item`/`.Iterator` projections, implicit method
receivers and method type parameters. Associated types can remain deferred when
a generic parameter or a method's inferred return is not yet known.
Defaults must have the existing literal/container/unary form. Contextual
`None`/`True`/`False` names are normalized in an owned output AST; shadowed literal
names are rejected. The input graph remains unchanged.

External signatures enforce the existing scalar/borrowed byte/numeric-buffer/
callback rules and C identifier restrictions. `declaration_checks.check(result)`
also detects conflicting external declarations, direct non-generic record cycles
through tuples/optionals, and direct record paths exceeding 256 edges. It is an
iterative traversal with cached longest-path heights. Container indirection breaks
direct containment. Generic records are not instantiated here: **this check does
not establish concrete generic layouts or full runtime resource/type validity**.

Signature success is not full type correctness. Default expression types,
reserved local/parameter bindings, body names/calls, return inference, control
flow, closures/captures, concrete generic instantiations and resource rules remain
later passes. Production performs the final record-layout check after body and
instantiation analysis; this isolated declaration stage deliberately checks only
the unspecialized graph. Its diagnostic priority must not be substituted for the
complete production analyzer's priority. Locks/source snapshots are not verified.

On failure, graph/declarations are discarded and the pool is reset to its two
sentinels. Codes/coordinates/messages match the corresponding production passes;
B3201/B3202 are bootstrap-only type-budget diagnostics. The pool budget is
2..1,048,576 nodes and counts unresolved/resolved intermediate types. Structural
depth is 64 and associated resolution depth is bounded at the same zero-based
128 ceiling as production. Budgets do not guarantee a fixed OS heap/stack limit.
Inputs must preserve loader/binder arena and edge invariants.

The versioned [semantic contract](semantic-contract.json) describes the new wire
schema. It projects reachable semantic types in deterministic child-first order,
with declaration IDs and readable names, declaration/method/parameter metadata,
and exact normalized default-expression events. Defaults and diagnostic messages
preserve raw bytes through hex encoding. `bodies_checked=false`,
`lock_verified=false`, and `layout_scope="unspecialized-declarations"` are explicit.
Missing arguments return 2, any frontend failure returns 1, and success returns 0.

The native gate compares complete results/diagnostics against actual C++
`resolve_signatures` and `validate_records`, not a mock semantic implementation.
It also compares 60 direct production API projection cases, including concrete
iteration/callable types, generic record protocols, deferred returns and recursion
rejection. Snapshot mutation and reduced budgets have native positive/negative
tests.

The optional native timing driver measures the complete project/module/parse/
binding/signature/unspecialized-check pipeline including destruction and warm
filesystem reads, excluding bundle decoding and output:

```sh
wood build bootstrap/src/signature_measure_main.wd -o build/wood-bootstrap-signature-measure
./build/wood-bootstrap-signature-measure build/bootstrap-stdlib.json 10 examples/mesh_import.wd
./build/Release/wood_bootstrap_signatures_oracle --measure 10 examples/mesh_import.wd
```

## Core expression semantics and preparation

`preparation_frontend.load(input, standards)` adds **literal-default type checks**
and **structural control flow** to the signature stage. It still returns
`bodies_checked=false` and `lock_verified=false`. It checks eager file-scope
function defaults and non-generic methods of non-generic records; instantiated
generic defaults and local closure defaults need the future body analyzer.
Generic templates remain deferred, preserving the production compiler's laziness.

`expressions.checker(tree, pool, names, declarations, ...)` supplies reusable
bidirectional typing for literals, names with supplied concrete type facts,
containers, operators, comparison chains, conditional expressions, indexing,
slices and fields. Call `checker.expression(id, expected)` for the actual type,
then `checker.require(actual, expected, token)` where assignment is required.
Optional hints supply a payload context; they never unwrap actual optional values.
Expression facts and normalized spellings live in an owned output snapshot.
Syntax indices, semantic pool IDs and field/index markers are different identities.

The kernel enforces signed-int64/finite-binary64 literal bounds, the special
unary-minus magnitude for INT64_MIN, invariant collections, permitted widening,
tuple arity/literal indices, dictionary key restrictions, numeric arrays and
equality restrictions. It does not evaluate expressions or remove runtime checks.
Fields can substitute concrete generic arguments with an explicit nominal
parameter-name context; this is not record instantiation or layout validation.

`return_types.infer` implements the production join rules for **already typed**
return sites, including numeric promotion, optional joins, nominal identity and
E3030 conflicts. It does not schedule inferred-return dependencies or type bodies.
`control_flow.check` produces statement exit masks and checked function summaries:
Next=1, Returned=2, Broken=4, Continued=8, Raised=16. It detects unreachable code,
invalid loop exits and explicit nonvoid fallthrough; unknown results are marked
`return_check_deferred`. Closure traversal starts a fresh loop/expression context.
Loops remain conservative and try/handler exits match the current C++ rules.

Calls, invocations, closures and comprehensions need later typing stages and
produce bootstrap-only **B3303** if passed to the expression kernel. Missing
lexical/call/resource/capture analysis is never silently marked complete. B3301
is an invalid visit limit; B3302 is visit exhaustion. Defaults share a visit
budget across modules; flow has one per module, each at most 1,048,576. These
limits are not fixed OS heap/stack bounds. Lower-level type interning may raise;
the preparation boundary translates type exhaustion to B3202 and discards partial
output. The input must preserve parser/loader arena invariants.

Build and run the native gate (Visual Studio developer shell on MSVC):

```sh
cmake --build build --config Release --target wood wood_bootstrap_semantics_oracle
ctest --test-dir build -C Release -R '^bootstrap_semantics$' --output-on-failure
```

The gate builds `wood-bootstrap-semantics` and native units in its unique output
directory, with an 8 MiB frontend stack on MSVC. Its two interfaces are:

```sh
wood-bootstrap-semantics --inspect build/bootstrap-stdlib.json bootstrap/src/semantic_query_main.wd
wood-bootstrap-semantics QUERIES.json
```

The inspection reply embeds the unchanged signature wire reply and adds typed
default events, structural flow masks and deferred counts. Query JSON is a test
and embedding-fixture adapter for isolated expression/default/return/flow rules,
**not a substitute for `wood check` on arbitrary source**. The C++ oracle calls
the actual production analyzer; exact diagnostics and typed expression events are
compared. The gate also locks, builds and runs a source-only catalog copy without
changing the original example or its lock.

See the [machine-readable contract](expression-contract.json).
Earlier signature APIs and contracts keep their original boundary. Preparation
checks defaults before flow, so its first error can differ from the complete
production analyzer when unrelated body/default errors coexist.

Optional fixture-throughput measurements require only development-time Python:

```sh
python benchmarks/bootstrap_semantics.py --wood PATH/wood-bootstrap-semantics \
  --reference build/Release/wood_bootstrap_semantics_oracle --iterations 5 \
  --samples 7 --work-dir build/semantic-measure-unique \
  --output build/semantic-measure.json
```

The reference checks whole fixture functions; the wood tool checks the named
kernel boundary. This is a partial-stage throughput study, especially for flow,
and cannot establish full-frontend parity, speedups or native-application performance.
