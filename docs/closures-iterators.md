# Closures and lazy iteration

wood compiles closures to native owned value objects. There is no Python dependency,
interpreter, tracing collector, or implicit shared mutable capture cell.

```wood
import std.iter as it

factor = 3
values = it.map(it.filter(range(100), fn(x) => x % 2 == 0), fn(x) => x * factor)
assert(it.collect(it.take(values, 4)) == [0,6,12,18])
```

`fn(parameters) => expression` infers its result. Parameter types may be omitted
when the call or annotation supplies their types; otherwise write `fn(x: int)`.
A closure captures only referenced outer locals, as owned immutable value snapshots.
Changing the original variable later does not change the captured value. Lists and
dictionaries retain their copy-on-write value semantics. Resource handles keep
existing shared identity/close semantics; capturing a stream does not extend a
`with` scope or make a closed stream usable.

Local named functions support ordinary blocks, defaults, keyword arguments and
explicit or [inferred results](return-inference.md). Factories can return concrete
closure types by omitting the result annotation; use `callable` when erasure is
desired. File-scope functions still cannot capture script locals.
Local functions cannot be generic or recursively refer to themselves.

```wood
fn counter(start: int) -> callable() -> int:
    value = start
    fn increment[mut value]() -> int:
        value += 1
        return value
    return increment

first = counter(10)
assert(first() == 11)
second = first
assert(second() == 12)
assert(first() == 12)
```

`[mut name]` gives the closure an **owned mutable copy**, never a reference to the
original local. All other captures remain immutable. An expression closure uses
`fn [mut state](x: int) => state.update(x)`. A stateful call requires a mutable
rooted path, or an owned temporary; it cannot mutate through an immutable capture
or a read-only `self`. Argument evaluation validates the target path before arguments
and reacquires it afterwards, retaining no element reference across a potentially
resizing argument. Copies of closures have independent value state; copied resource
handles still alias their resource.

Concrete inferred closure types retain their own native struct and static call
operator. Use `callable(T, ...) -> R` for heterogeneous collections, stored APIs and
escaping factory results. In the C++23 backend this owns a copyable callable with 32 bytes of inline
capture storage; larger, over-aligned, or potentially throwing-move targets use
owned heap storage. Calls through this explicit erased type use a dispatch table.
The [LLVM native profile](native-backend.md) uses inline concrete closures and
reference-counted heap boxes with COW detachment for erased callable targets.
These are backend storage choices; observable capture/value semantics agree.
`fn(T, ...) -> R` remains a raw noncapturing native function pointer, including for
C callbacks. Capturing closures cannot cross the existing C callback ABI.
Use `(callable(int) -> int) | None` for an optional callable; parentheses group
annotations and distinguish it from a callable returning an optional.

Erased callables/function pointers accept positional arguments only. Concrete
closures retain their parameter names and declaration defaults. Closures, erased
callables, cursors and ranges have no equality or JSON serialization; containers
and records containing them have the same restriction. Closure captures are bounded
at 255, lexical closure nesting at 64, and direct owned capture containment at 256.

## Generic callable patterns

A named type parameter can constrain a callback without erasing its concrete type:

```wood
fn transform[S,F](source: S, callback: F(S.Item)) -> list[F.Result]:
    return [callback(item) for item in source]
```

`F(A,B) -> R` checks the parameter and result types; omitting the result arrow
infers it. `F.Result` is the result type, `S.Item` the iteration item, and
`S.Iterator` the constructed cursor type. These are supported associated types,
not a general trait/overload system. Inference can obtain callback context from
later arguments without changing left-to-right runtime evaluation. If neither
other arguments nor the annotation determine a parameter, annotate it explicitly.
Existing eager `std.functional` helpers accept capturing callbacks as well.
The existing `std.collections.Heap[T]` comparator remains a raw `fn` pointer; its
annotation/API is preserved rather than silently imposing erased dispatch on it.

## Iterator protocol

A cursor supplies `fn next(mut self) -> step[T]`. A reusable iterable supplies
`fn iter(self) -> CursorType`; `iter` has no other arguments and is read-only.
Neither protocol method may have additional method type parameters.

```wood
record Countdown:
    value: int
    fn next(mut self) -> step[int]:
        if self.value == 0:
            return finished()
        self.value -= 1
        return yielded(self.value)

cursor = Countdown(3)
assert(step_value(next(cursor)) == 2)
assert([item for item in cursor] == [1,0])
```

`step[T]` contains either `yielded(value)` or `finished()`. Check `has_item(step)`
and extract using `step_value(step)`; extraction after exhaustion raises R4024.
`None`, including an absent optional value, can be a **present item**. `finished()`
requires a `step[T]` context. The protocol does not enforce termination or sticky
exhaustion on user-defined cursors; implement that behavior in your cursor.

`iter(list/string/dictionary/range)` constructs an owned native cursor. Lists retain
an inexpensive COW snapshot, strings own a copy, and dictionary cursors snapshot
insertion-ordered keys. `cursor[S]` annotations name the source collection type,
for example `cursor[list[int]]`. Explicit records named `step` or `cursor`
retain annotation priority, preserving existing user-defined types. `range(...)` is now a first-class value; direct
range loops retain their previous lowering. `iter(cursor)` copies its current
position. Explicit `next(cursor)` mutates that path. `for` and comprehensions
iterate an owned snapshot of the cursor, so they do not advance the original.
Stream aliases are an exception to value isolation: reading advances their shared
OS stream position.

The backend lowers cursor loops to direct `next` calls and step checks. Generic
adapters specialize the complete source and callback types. They need no erased
iterator interface, coroutine frame, or intermediate list. Optimized host compilers
may inline the stages; this is not a promise that every pipeline matches a hand
written loop. See the reproducible [benchmark](../benchmarks/run_lazy_pipelines.ps1)
and its measured limitations in [performance.md](performance.md).

## `std.iter` API

All factories take the source first; construction does not read items.

| Function | Behavior |
| --- | --- |
| `map(source, transform)` | Transform each demanded item; infer result type |
| `filter(source, predicate)` | Demand items until one passes a bool predicate |
| `take(source, amount)` | At most amount items; never read the following item |
| `drop(source, amount)` | Skip amount items on first demand, then forward |
| `enumerate(source, start=0)` | Yield `(index,item)`; checked index increments |
| `zip(left, right)` | Stop at the shorter source; demand left before right |
| `chain(left, right)` | Consume left then right; identical item types |
| `from_fn(producer)` | Zero-argument callback returning step values; sticky exhaustion |
| `lines(source, limit=1048576)` | Stream lines including LF; empty string from the stream means EOF |
| `collect(source)` | Materialize a native list |
| `fold(source, initial, combine)` | Reduce with a concrete accumulator type |
| `count(source)` | Count all items |
| `any(source, predicate)` / `all(source, predicate)` | Short-circuit on the first decisive item |
| `find_first(source, predicate)` | Return first matching step or finished |

Negative take/drop amounts and nonpositive line budgets raise R4010. Line budgets
are byte limits passed to the existing bounded stream reader. The caller owns the
stream, normally with `with`; the adapter never closes it. Zip may consume one extra
left item when the right source finishes. Adapters copy the source position and
callback state on construction. Reiterating a value pipeline starts from that
stored position; reiterating a shared stream reads from its current OS position.
String/list payloads still obey ordinary value-copy semantics when yielded or
passed to callbacks; this is not a borrowing iterator API. `yield` generators,
reference captures, async iteration, variadics and bound method values remain
unsupported.

Internally, `step_value` can move the payload of a temporary or a proven final-use
owned step with a scalar, string or numeric-array payload. Resource/aggregate
payloads are excluded from last-use moves, preserving observable resource lifetimes.
Extracting a step that remains live still preserves its value.
Returning a comprehension does not consume its shared inputs on the first
iteration. These optimizations add no references to the public iterator protocol.

The [log report application](../examples/log_report.wd) parses bounded JSON-lines,
filters with a captured threshold, stops at a requested match count and aggregates
by service using mutating record methods. It keeps no intermediate event list;
only the line, pipeline state and per-service aggregate table are retained.

Run the supplied application from the repository root:

```sh
wood examples/log_report.wd -- examples/access_log.jsonl report.json 40 100
```

The sample selects five requests across three services. API totals are two
requests, one failure and 184 ms; worker totals are two requests, no failures and
197 ms; auth totals are one request, one failure and 92 ms. Output is written only
after successful parsing and aggregation. Its final `write_text` retains normal
truncation semantics; this is not an atomic publication/durability protocol.
