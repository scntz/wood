# Native typed abstractions

wood supports inferred generic functions, fixed heterogeneous tuples, optional
values, and named/default arguments. These compile through the ordinary native
backend; they have no Python runtime dependency.

`fn` is the canonical spelling throughout the native library and examples.
`def` remains an optional compatibility alias. These features follow wood's
static types, value semantics and explicit native contracts; shared concepts are
judged by consistency and usability rather than syntactic similarity to Python.

## Tuples and optional values

```wood
fn divide_parts(total: int, size: int) -> tuple[int, int]:
    return (total // size, total % size)

parts = divide_parts(17, 5)
assert(parts[0] == 3 and parts[-1] == 2)
quotient, remainder = divide_parts(17, 5)
quotient, remainder = remainder, quotient
assert(quotient == 2 and remainder == 3)
for name, count in [('pears', 3), ('figs', 5)]:
    print(name, count)
empty: tuple[()] = ()
single: tuple[str] = ('item',)

maybe: int | None = None
maybe = 7
if maybe is not None:
    print(maybe + 1)
```

Tuples are immutable values containing independently typed fields. Parentheses
and a comma distinguish a singleton from grouping. Indexes must be integer
literals, optionally signed, and are checked at compile time. `len`, `str`,
printing, equality/inequality, containers, function values, records, and JSON
conversion support tuples. Nested containers retain copy-on-write isolation.
Tuple arity is limited to 255.

A `None` check narrows a local variable or parameter to its payload type, so no
`unwrap` is needed: inside `if x is not None:` (also when it is the first operand of
`and`), in the `else` of `if x is None:`, and for the rest of the block after an
`if x is None:` with no `else` whose body never falls through (it returns, raises,
breaks or continues). Other parts of the condition still see the optional. Assigning
`x` ends the narrowing and writes the original variable, and a loop that assigns `x`
ends it before the loop. A narrowed `x` cannot be changed in place, with `append(x, ...)`,
`x[i] = ...`, `x += ...` or a `mut` capture (E3015, E3028), because that would change
a copy; assign it instead. Record fields and other expressions are not narrowed.

Unpacking assigns a tuple's values to names, in assignments (`a, b = pair`, or
`a, b = b, a` to swap) and in `for` loops (`for name, count in pairs:`). The value
must be a tuple with exactly as many values as there are names, checked at compile
time (E3025). Each name follows the ordinary assignment rules: a new name is declared,
and an existing one keeps its type. The whole right-hand side is evaluated before any
name changes. Unpacking does not nest, and comprehension clauses take a single name.
Tuples do not yet support slicing, iteration, ordering, or concatenation.
Very wide tuples can incur substantial host C++ template compilation work;
the maximum-arity native regression runs in isolation within CTest.

`T | None` is a native optional value, represented by `std::optional<T>` in the
C++ backend. `None` and a compatible `T` can initialize/replace it; int widens to
float when initializing `float | None`. A present optional cannot implicitly
convert to its payload. `unwrap(value)` copies the contained value and raises
R4023 at the call site if absent. There is no implicit truthiness or flow-sensitive
narrowing yet. `is None` / `is not None` test presence, in either operand order;
identity comparisons between other values are rejected. Equality also accepts
compatible optional/payload/None operands. Conditional expressions join a value
and None into an optional. Other union types are unavailable.

JSON conversion maps absence/None to null and tuples to arrays, recursively.
Native handles and functions remain ineligible for JSON even inside these types.
Formatting prints None as `None`; existing lowercase boolean formatting remains.
Explicit existing variables/functions named `None` retain precedence over the
literal, as with `True`/`False`. A record named `None` retains its nominal type;
the `| None` suffix always denotes absence.

Optional and tuple fields directly contain their records: recursive record
cycles still need list/dictionary indirection. For example, `list[Node]` is valid
inside Node; `Node | None` alone cannot make a finite native record layout.

## Generic functions

```wood
fn pair[A, B](left: A, right: B) -> tuple[A, B]:
    return (left, right)

fn first[T](items: list[T]) -> T | None:
    if len(items) == 0:
        return None
    return items[0]

fn identity[T](value: T) -> T:
    return value

assert(pair(right=2, left='a') == ('a', 2))
callback: fn(int) -> int = identity
```

A concrete `fn` annotation can select a generic function specialization for storage/passing.
An unconstrained generic function is not a runtime value.

Type parameters are local to a function signature/body. Inference structurally
matches argument types, including nested lists/dicts, tuples, optionals and
function signatures. Repeated parameters require exactly the same type; this
does not silently widen containers or merge int/float into one type parameter.
Parameters infer in written argument order; subsequent arguments can use already
inferred types to type empty literals. None alone cannot infer a payload type.
A concrete expected result can infer otherwise unbound return parameters.

Each distinct concrete type-argument vector produces one checked native function
per generic declaration. Repeated/recursive calls reuse it. Imported bodies resolve
names in their defining module. Bodies are type checked on instantiation, including
operator requirements and return paths; unused generic bodies are not certified.
Errors identify the source expression, not a downstream C++ template failure.

There is no type erasure or runtime type dispatch in specialization itself.
Ordinary native function-pointer callbacks still have their existing calling
contract. Code size and compile time grow with distinct instantiations; this is
not a performance parity claim. Limits are 32 type parameters per function,
64 active instantiations, 1024 distinct instantiations per compilation, and the
existing 64-level structural type limit.

Generic records and explicit record constructor types are documented in
[generic-records.md](generic-records.md). Classes, explicit function type arguments, bounds/constraints,
variadic type parameters and full bidirectional constraint solving are future
work. Annotate an empty collection or a generic callback when inference has no
concrete type to start from.

## Calls

```wood
fn row(code: int, /, label: str = 'item', *, count: int = 1) -> tuple[int,str,int]:
    return (code, label, count)

assert(row(7, count=2) == (7, 'item', 2))
assert(row(7, label='book', count=3) == (7, 'book', 3))
```

Names before `/` are positional-only. Names after `*` are keyword-only. Other
parameters accept either form. Required keyword-only parameters may follow
defaulted ones. Required positional parameters cannot follow defaulted positional
parameters, including across `/`. Supplied arguments evaluate once in written
order before being placed in their parameter positions. Positional arguments
must precede keywords. Missing, duplicate, unknown and positional-only keywords
are frontend errors. Record constructors accept field keywords as well.

Defaults are scalar literals, None/boolean literals, unary literal expressions,
or nested tuples/lists/dicts of these values. They are checked against parameter
types. Calls, variable references, binary computations and definition-time effects
are rejected. Each call receives an independent value; mutable defaults cannot
accumulate state across calls. This deliberately follows wood's value semantics.
Imported modules still have no executable initialization phase. Default literal
aliases cannot refer to same-named function declarations.

Named declaration calls carry parameter names/defaults. Structural `fn(...) -> R`
values carry only invariant types: calling one requires all arguments positionally,
even if the source declaration has defaults or keyword-only parameters. Builtins
also retain positional APIs. `*args`, `**kwargs` and argument unpacking are not
implemented. Owned [closures and local functions](closures-iterators.md) are supported;
concrete closures preserve declaration call modes, erased callables are positional. Native record methods support
these named call modes; see [generic-records.md](generic-records.md).

Python's [call binding](https://docs.python.org/3/reference/expressions.html#calls)
and [function signatures](https://docs.python.org/3/reference/compound_stmts.html#function-definitions)
provide comparison points for shared behavior, not a grammar for wood.
Python evaluates arbitrary defaults at definition time
and shares mutable default objects; wood's restricted value defaults intentionally
differ. Python typing does not enforce wood's static specialization rules.

## Native algorithms and tooling

`std.functional` implements `map`, `filter`, `fold`, `first`, `find_first`, `zip`,
`enumerate`, `partition`, and dictionary `lookup` in wood. Lists are eager value
results, zip stops at the shorter input, and optional searches return None when
absent. Predicates run once per visited element; partition preserves order in
each output. Enumerate uses checked integer addition. These are not lazy Python
iterator objects. See [stdlib.md](stdlib.md).

`wood inspect` adds `type_parameters`, parameter `mode` and `has_default`, and
structural tuple/optional/type-parameter descriptors. Existing fields remain.
`tests/typed_interfaces.wd` covers native behavior; the optional Python oracle
compares shared tuple/optional/algorithm/call behavior and invalid call binding.
`examples/typed_catalog.wd` validates input before producing a typed JSON report.

From the repository root, run
`wood examples/typed_catalog.wd -- examples/typed_catalog_input.json report.json`.
The sample totals 1700 cents, with one categorized and two uncategorized products.
The application validates all products before opening the output; rejected input
therefore preserves an existing report. Successful writes retain normal
`write_text` truncation semantics and are not an atomic replacement protocol.
