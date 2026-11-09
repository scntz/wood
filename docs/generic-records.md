# Generic records and methods

wood records are nominal values with native layouts. A generic record declares
its type parameters after its name, and supplies ordinary typed fields and `fn`
methods in one indented body:

```wood
record Box[T]:
    value: T

    fn get(self) -> T:
        return self.value

    fn set(mut self, value: T):
        self.value = value

    fn map[U](self, convert: fn(T) -> U) -> Box[U]:
        return Box[U](convert(self.value))

box = Box(5)
box.set(value=7)
empty: Box[list[int]] = Box([])
explicit = Box[list[str]]([])
```

`Box[int]` and `Box[str]` are different, invariant types. Records with the same
name in different modules are also different types. Imported aliases preserve
the original identity; they never create a new specialization. All constructor
fields remain required and retain positional/keyword binding and numeric
widening. Methods are not constructor fields. Existing field-only records keep
their syntax and behavior.

## Inference and checking

Constructor arguments infer record parameters structurally, in written order.
A concrete expected record annotation or explicit constructor application supplies
parameters before checking arguments, allowing empty collections, absent optional
values, and unused type parameters. Use `Queue[int]([], [])` or an annotated
initializer when no argument determines the payload type. Without a concrete
context, repeated inferred parameters must match exactly. There is no general
unification pass over later arguments: an untyped empty first argument still needs
a concrete context. Explicit applications currently belong to record constructors;
`function[int](...)` and stand-alone record type values are unsupported.

Generic functions can accept and return records such as `Box[T]`, infer `T` from
concrete instances, and return new specializations. Methods may declare additional
parameters (`fn map[U](self, ...)`), inferred by the same generic function rules.
A method parameter cannot shadow a record type parameter. Function and record
type parameters each have a limit of 32. The existing limit of 255 function
parameters includes the implicit receiver parameter of a method.

Signatures and member-name conflicts are checked at declaration time. Nongeneric
record methods are checked even when unused. Generic record fields are checked
for each instantiated type; its methods are checked when called. Generic method
bodies are checked on specialization. Invalid unused generic bodies can therefore
remain undiscovered until instantiated, just as with generic functions. Diagnostics
retain definition locations and include method/instantiation context where available.

Concrete specializations use the defining module's imports and types, including
across locked local packages. The compiler caches by nominal identity and exact
type arguments. Record and generic function instances share the existing budget
of 1024 instantiations and 64 active instantiation levels. Type argument nesting
also obeys the 64-level structural type limit. Recursive records require a list
or dictionary indirection, including after substituting type parameters. Expanding
recursion such as `Grow[T]` containing `list[Grow[list[T]]]` is bounded and rejected.

## Receivers and mutation

Every method starts with unannotated `self` or `mut self`. `self` is a positional-only
receiver supplied by `value.method(...)`, never a caller-supplied keyword.
Other parameters support annotations, literal defaults, `/`, `*`, and keywords
like named functions. Nested functions remain unsupported.

- `fn read(self)` receives a value snapshot before argument evaluation. It can
  return fields or modify independent local copies, but cannot assign `self`,
  mutate a path rooted in it, call a mutating method on it, or pass such a path
  to a mutating builtin. E3027 explains that `mut self` is required.
- `fn change(mut self)` receives temporary native access to the caller's record.
  The receiver must be a variable or a rooted field/list/dictionary path; a
  temporary such as `Box(1).set(2)` is rejected. Mutation never returns a reference.
- Ordinary function parameters retain their existing value semantics. Record,
  list and dictionary copies isolate later structural mutations. Native resource
  handles inside a record still alias their resource identity; a read-only
  receiver does not freeze an OS resource or prohibit I/O through a handle.
- A mutating call evaluates the receiver's index expressions once, validates the
  path before arguments, evaluates arguments in written order, then reacquires
  the captured path for method entry. No element reference survives argument
  evaluation. If an argument replaces a record at that path, the method operates
  on the replacement. If it removes the path, entry fails with the usual bounds
  or missing-key error. Negative indexes retain their captured integer value and
  are interpreted against the collection when reacquired.
- Mutations already performed before an exception remain visible. Calls do not
  provide transactional rollback. Aliased resource effects follow the same rule.

Read methods can be called on temporary values. Methods are immediate calls,
not bound function values or implicit closures: `saved = box.get` is rejected.
Function-valued record fields remain ordinary native callbacks and keep their
existing call behavior. Method names cannot duplicate fields or other methods.
There is no method overloading, inheritance, static method syntax, access control,
trait/constraint system, operator customization, or dynamic dispatch.

## Native representation and tooling

Each used generic record has a concrete generated C++ struct. Each used method
has a concrete native function; mutable receivers use a reference, and read
receivers use value parameters. There is no interpreter, runtime generic dictionary,
Python dependency, type erasure, or virtual method dispatch. Optimization remains
the host C++ compiler's responsibility; this is a representation guarantee, not
a benchmark claim. Method bodies are checked with a worklist, so long chains of
ordinary method calls do not consume nested compiler analysis frames.

`wood inspect` describes record `type_parameters`, typed `fields` and `methods`.
Nominal record descriptors include their definition `module`, `name`, and
structural `arguments`. Method descriptors include `receiver` (`value` or
`mutable`), parameters (including `self`), defaults, additional type parameters,
and the return descriptor. Existing function descriptors use receiver `none`.
`wood --language-info` advertises these capabilities for coding agents.

## Reusable native collections

[`std.collections`](../stdlib/collections.wd) implements three value collections
entirely in wood:

| Type | Operations | Algorithmic work with unshared storage |
| --- | --- | --- |
| `Stack[T]` | `push`, `pop`, `peek`, `size`, `clear` | Append/pop at the list end; amortized constant push |
| `Queue[T]` | `push`, `pop`, `peek`, `size`, `clear` | Two stacks; amortized constant push/pop; transfer is linear |
| `Heap[T]` | `push`, `pop`, `peek`, `size`, `clear` | Binary heap; logarithmic push/pop; constant peek |
| `heap(items, before)` | Build a `Heap[T]` | Bottom-up linear heap construction |

These are algorithmic bounds, not measured throughput claims. Copy-on-write
mutation of shared backing lists can additionally copy all elements. A record's
read snapshot can also copy its scalar/string fields. `peek` returns `T | None`;
`pop` on an empty collection raises R4007. Comparators are native `fn(T,T) -> bool`
values and must implement a consistent strict ordering without raising. A raising
comparator can leave a partially modified heap; operations do not provide rollback.
The stated bounds count payload operations/comparisons; copying a large payload
has its own cost. Equal-priority heap items
are not stable unless the comparator includes a tie breaker. Recursive payloads,
optional values, tuples and user records are supported. Fields remain public:
use methods to maintain queue and heap representation invariants. Construct
`Heap` with `heap(items, before)` or with an already valid heap layout.

[`route_planner.wd`](../examples/route_planner.wd) uses `Queue[str]` for reachability
and `Heap[Visit]` for Dijkstra's shortest paths. Run:

```text
wood examples/route_planner.wd -- examples/routes.json routes-output.json
```

The program validates nonnegative edge costs and destination names, reports
reachable places and concrete cheapest paths, and checks integer overflow. It
computes before opening the output so rejected inputs preserve existing output.
The test suite compares it with an independent Floyd-Warshall oracle on 12 graphs,
including zero costs, cycles, repeated improvements and disconnected vertices.
It plans routes in memory; it isn't a routing service.
