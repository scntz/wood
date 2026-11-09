# Function return inference

wood can infer a function's result while keeping explicit parameter types and
static native code. This applies to file-scope functions, generic specializations,
record methods and local named functions. Explicit `-> R` annotations remain useful
for public contracts, context for literals, and recursive dependencies.

```wood
fn square(value: int):
    return value * value

fn identity[T](value: T):
    return value

fn adder(offset: int):
    return fn(value: int) => value + offset

add_five = adder(5)
assert(add_five(square(3)) == 14)
assert(identity((1,"wood")) == (1,"wood"))
```

`adder` returns a concrete generated closure struct. The snapshot of `offset` owns
its value after the factory returns. No `callable` conversion, dispatch table or
closure allocation is introduced by return inference. A capture can itself own
allocating data; inference does not make that storage free. Explicit assignment
to `callable(int) -> int` still requests the existing erased representation.

## Deterministic result rules

The compiler checks every reachable return once, then joins its actual types:

- Identical types stay identical, including nominal record and closure identity.
- `int` and `float` join to `float`, with existing numeric return conversion.
- `None` and a value type `T` join to `T | None`. An existing optional can join
  with compatible values, but every original return must be assignable to the
  final result. `int | None` and `float` are rejected: the language does not
  implicitly convert an already constructed optional's payload.
- Only `None` returns produce the value type `None`, distinct from `void`.
- No value returns produce `void`. Mixing bare returns with value returns fails
  with E3030. Unreachable statements retain their existing E3008 diagnostic.
- Incompatible results fail with E3030. Containers are invariant. Two different
  closure expressions have distinct types; an explicit `callable(...) -> R`
  result can provide a common representation when their signatures agree.

All paths of a value-returning function must return or raise (E3013). There is
no implicit `None`; a loop conservatively may execute zero times. Bare returns
inside nested closures do not influence their enclosing function.

```wood
fn lookup(found: bool):
    if found:
        return 7
    return None

fn numeric(flag: bool):
    if flag:
        return 2
    return 3.5

assert(unwrap(lookup(true)) == 7)
assert(numeric(true) == 2.0)
```

## Dependencies, methods and modules

An inferred function can call a later declared inferred function. Bodies are
checked on demand in their **definition module**, with results cached in the
existing declaration/specialization. Generic results are inferred separately
for each concrete specialization. Additional generic method parameters also
support concrete callable patterns.

```wood
record Box[T]:
    value: T
    fn read(self):
        return self.value
    fn transform[F](self, callback: F(T)):
        return Box(callback(self.value))

assert(Box("native").transform(fn(value) => len(value)).read() == 6)
```

Inferred `iter`/`next` method results participate in `.Iterator` and `.Item`
resolution. The existing method arity, receiver mutability and `step[T]` protocol
requirements still apply. `finished()` alone cannot reveal `T`: use an explicit
method result `-> step[T]` or a typed local before returning it.

Recursive return dependencies are deliberately not solved by speculative fixed
points. E3030 identifies a dependency cycle; annotate at least one function to
break it. This preserves ordinary annotated direct/mutual recursion. There are
at most 64 active return-inference dependencies (E2002), separate from the existing
64 active generic-instantiation limit. This is a compiler resource bound, not a
native runtime recursion limit.

Ordinary function results do not depend on caller expected types. `return []`,
`return {}`, and `return finished()` need an explicit result annotation or a typed
local. Return-only generic parameters still need explicit type information.
Expression closures retain their existing contextual result inference.

Use `wood explain E3030` for a concise offline explanation and a tested recursive
example. Its annotation fixes that recursive dependency; incompatible result
values still need compatible types, and missing paths remain E3013.

## Compatibility and tooling

Previously valid no-value functions still return `void`; annotated functions keep
their contracts. `extern fn` without an arrow still returns `void`, preserving C
ABI declarations. Omitted annotations on value-returning blocks now accept source
that was previously rejected. `def` remains an optional alias; `fn` is canonical.

`wood inspect` adds `return_inferred` and `return_resolved` booleans to function
and method metadata. An uninstantiated generic can have an unresolved result;
tools must not invent a concrete type for it. `wood --language-info` describes
the rules and limits. No Python runtime or inference service is involved.
