# Coming from Python

wood aims to be as simple to write as Python, with its own syntax and native semantics. A lot will look familiar:
indentation, `for` over collections, comprehensions, slices, `and`/`or`/`not`, records that feel like dataclasses. But
Python code doesn't run unchanged, and some differences matter. The [language reference](language.md) is the authority;
this page is a guide.

To use Python libraries from wood, see [Python packages](python-distribution.md).

## Familiar spellings

- `fn` is the way to write a function. `def` also works, and so do `True` and `False`, unless you've defined something with
  those names. Function types are still written `fn(T) -> R`.
- Comparison chains such as `0 <= i < n` evaluate each operand once and stop early.
- `x if condition else y` evaluates the condition first and then one branch.
- `+=` and the other augmented assignments evaluate the target and its indexes once, before the right side.
- Slices take optional bounds and steps, including negative steps. An omitted negative bound isn't the same as an explicit
  `-1`, and a step of 0 raises R4005. Slice assignment isn't supported.
- List and dictionary comprehensions, with nested loops and filters. Their variables don't leak out. In a dictionary
  comprehension, keys are evaluated before values, and a duplicate key keeps its first position and takes the last value.
- `+` concatenates lists, and `*` repeats them.

## Differences that matter

| wood | What it means when porting |
| --- | --- |
| Static types, typed parameters | Named functions have literal defaults and keywords, but function values are called positionally. No `*args`, `**kwargs` or decorators |
| `int` is a checked 64-bit integer, `float` is binary64 | Overflow is an error, not a bigger number |
| Lists, dictionaries and records are values that copy on write | Changing a parameter doesn't change the caller's object, and `[[0] * 3] * 3` gives independent rows. Return the new value or use a `mut self` method |
| Strings are bytes | Indexing and slicing can split a UTF-8 character. Use `std.utf8` for characters |
| Block scopes, and functions can't see script variables | Pass values in explicitly. Closures capture copies |
| Built-in names are mostly reserved for functions | Shadowing built-ins like Python does isn't supported everywhere |
| Conditions must be `bool` | No truthiness: write `len(items) > 0`, not `if items`. `and` and `or` return `bool` |
| Records, generics, closures and iterators, but no classes | No inheritance or generic constraints yet |
| One error type with your own codes, `finally` and `with` for cleanup | No exception classes or tracebacks, and no user-defined context managers |

## What's coming

These are plans, in rough order, and none of them works yet. See [the roadmap](roadmap.md).

1. Generic constraints, narrowing optionals, and unpacking tuples.
2. Argument unpacking and richer function values.
3. Generators and coroutines, and resources you define yourself.
4. Unicode text as a separate type, more numeric types, and errors with context.
5. What it takes to port libraries: typed buffers, C bindings with ownership, and packaging.
