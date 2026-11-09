# wood language reference

This is the reference for the wood language, version 0.4.0. Future plans are in [the roadmap](roadmap.md) and aren't part
of the language. For a gentler start, read [the tutorial](tutorial.md); the [documentation home](README.md) has a topic index.
The pages on [types and calls](types-and-calls.md), [records](generic-records.md),
[closures and iterators](closures-iterators.md), [data](data-model.md) and [arrays](native-computing.md) are part of this reference.

wood has its own syntax and semantics. `fn` is the way to write a function; `def` and the capitalized `True` and `False`
are optional aliases. Python is a guide to simplicity, not to grammar or behavior.

The rules here are the same whichever compiler you use: the C++23 reference compiler of a source build, or the compiler
written in wood that the installers ship, with its [LLVM backend](native-backend.md). If the LLVM backend can't build
something, it says so with B3501 instead of falling back to C++. The [support matrix](support.md) lists what each backend
covers.

The LLVM backend also supports `import python@module [as alias]` and the `py_object` type, with explicit `std.python` calls
and conversions. The C++ reference compiler doesn't. See [Python packages](python-distribution.md).

## Source and statements

Files use `.wd`. Identifiers use ASCII letters, digits, and `_`, and cannot start
with a digit. Names are case sensitive. Strings and comments can contain UTF-8.
Diagnostics use one-based lines and byte columns. LF, CRLF, a final line without
a newline, and a leading UTF-8 BOM are accepted.

Unknown-name errors may suggest a unique nearby visible spelling. Call errors
describe expected parameters/counts and supplied arguments; ordinary type
mismatches show expected/actual types and point at the value expression. These
are diagnostic improvements, not implicit corrections or coercions. See
[diagnostic contracts](cli.md#diagnostics-and-exit-codes) for scope and structured
output; error codes and the JSON diagnostic schema remain stable.

`wood explain CODE` supplies offline explanations and checked examples for a small
set of existing errors, including E3030. It changes no language rules; see the
[CLI reference](cli.md#diagnostics-and-exit-codes) for coverage and output behavior.

`#` begins a comment outside a string. Blank and comment-only lines do not affect
indentation. Spaces delimit blocks; any consistent indentation width works.
Tabs outside strings are rejected. A block begins with `:` and a newline and
must contain a statement; use `pass` for an empty block. Expressions inside
parentheses, square brackets, or dictionary braces can span physical lines. Semicolons, backslash continuations, and
single-line suites are not supported.

The CLI limits source files to 16 MiB. Indentation and expression nesting are
bounded (at most 255 nested indentation blocks and 256 expression levels).
Functions and calls accept at most 255 parameters/arguments. These are explicit
compiler resource limits, not performance targets.
Conditional nesting, including an `elif` chain, is also bounded at 256 levels.
Structural list/dict/tuple/optional/function type nesting, whether annotated,
inferred or specialized, is limited to 64 levels.
Direct record containment is limited to 256 edges on every containment path,
independent of declaration order. Container indirection is not a direct edge.

## Types and variables

| Type | Representation and behavior |
| --- | --- |
| `int` | Signed 64-bit integer, from -9223372036854775808 to 9223372036854775807 |
| `float` | IEEE 754 binary64 (`double`) |
| `bool` | `true` or `false`; no implicit truthiness |
| `str` | Immutable byte string with value semantics; UTF-8 text is conventional |
| `list[T]` | Homogeneous dynamic list with value semantics and copy-on-write storage; nesting allowed |
| `array[T]` | Fixed-shape int/float numeric value; contiguous ownership and strided snapshot views, rank 1..8 |
| `dict[K, V]` | Homogeneous insertion-ordered dictionary; int/str/bool keys; copy-on-write values |
| `tuple[A, B, ...]` | Fixed heterogeneous immutable value; `tuple[()]` is empty |
| `T \| None` | Optional native value; explicit `unwrap`, absent values print None |
| Named record / `Record[T]` | Nominal generic value with typed fields and native methods |
| `json` | Immutable heterogeneous JSON tree; explicit conversion at data boundaries |
| `error` | Immutable error value with code, message, file, line, and column fields |
| `stream` | Shared identity handle to an open/closed file, TCP connection or TCP listener |
| `database`, `statement`, `transaction`, `tls_context`, `task` | Shared native identities; scoped cleanup and library-specific validity rules |
| `fn(T, ...) -> R` | Non-null native function pointer; invariant parameter/result types |
| `callable(T, ...) -> R` | Owned callable; inline small captures, explicit erased dispatch |
| `step[T]` | Present yielded value or exhausted iterator step |
| `cursor[Source]`, `range` | Owned native iteration state |
| `void` | No value; only a function return type |

An initializer is required. First assignment declares a variable in the current
lexical block and infers its fixed type:

```wood
count = 1
count = count + 1
ratio: float = count
```

An annotation can appear only on the first assignment. An assignment to a visible
variable changes that variable; it does not introduce a shadow. New declarations
inside a branch or loop are not visible outside that block. Initialize an outer
variable before branching if it must be available afterward. A `for` variable is
a fresh loop-local binding and may shadow an outer variable. Changing it in the
loop does not change range progression.

`int` widens implicitly to `float` in annotated initializers, assignment, function
arguments, and returns. None and compatible payloads can initialize optional
values. Other implicit conversions are rejected. Converting a
large integer to binary64 can lose precision beyond 2^53. Function and historical built-in
names are reserved from variable declarations, including parameters and loop
variables. Historical type names remain reserved. `dict`/`json` annotations use the type
namespace; existing locals can keep those names. New dictionary/JSON/slice helper
calls resolve after explicit user/imported functions and do not reserve locals.
The word `record` is contextual before a declaration name.
`stream` annotations use the native handle type unless an explicit record binds
that name; existing records named `stream` keep their nominal identity.

## Literals and operators

Integers are decimal, including literals with leading zeros. Floats contain a
decimal point or exponent: `1.25`, `2e3`, `1.0e-2`. A decimal point requires digits
on both sides. Hexadecimal, digit separators, and numeric suffixes are unsupported.
Out-of-range integer and nonfinite/out-of-range float literals are compile errors.
The minimum integer is accepted through unary minus applied to its magnitude.
`True` and `False` are compatible boolean spellings alongside `true`/`false`;
explicit same-named variable/function bindings retain precedence.

Strings use `'single'` or `"double"` quotes. Escapes are `\n`, `\r`, `\t`, `\0`,
`\\`, `\'`, and `\"`. Unknown escapes and physical newlines inside strings are
errors. Embedded NUL bytes are preserved. There is no string interpolation.
String indexing accepts an integer (including negative offsets) and returns a
one-byte string; out-of-range indexes are runtime errors. Strings are immutable,
so indexed assignment is supported on lists and dictionaries. Record fields can
be assigned through rooted paths.
`len` counts bytes, not Unicode characters. Concatenation creates a new value;
ordering compares bytes lexicographically. No Unicode normalization is performed.

The compiler may borrow values internally for read-only comparisons, indexing
and slicing when later operand evaluation cannot structurally mutate them. Owned
string temporaries may reuse their buffers during concatenation. Once-used owned
strings and steps with scalar/string/numeric-array payloads in a final top-level
return may move; resource-bearing/aggregate steps, closures,
comprehensions, captures and local handler paths are excluded. These are internal
optimizations: ordinary assignment, calls and iterators retain their documented
snapshot/value semantics, and no source borrowing or `move` syntax is exposed.
Direct range loops with default/literal stride 1 or literal -1 can use native
bounded induction. The exclusive bound proves the final increment/decrement
cannot overflow; assigning the loop binding still does not alter progression.

Operators, from lowest to highest precedence:

| Operators | Rules |
| --- | --- |
| `or` | Two booleans; evaluates right only if left is false |
| `and` | Two booleans; evaluates right only if left is true |
| `not` | Boolean negation; `not a == b` means `not (a == b)` |
| `== != < <= > >=` | Same value types, or mixed numeric types |
| `in`, `not in` | Same comparison precedence; dictionary keys, list elements, string substrings |
| `\|` | Bitwise OR on two integers |
| `^` | Bitwise XOR on two integers |
| `&` | Bitwise AND on two integers |
| `<< >>` | Integer shifts; addition binds more tightly |
| `+ -` | Numeric addition/subtraction; `str + str` concatenates |
| `* / // %` | Numeric multiplication/division; `//` and `%` require integers |
| unary `+ - ~` | Numeric signs; bitwise complement requires int |

Binary arithmetic operators associate left. Comparisons such as `a < b <= c`
chain: reached operands evaluate once, and evaluation stops at the first false
comparison. Membership comparisons can participate. `(a < b) < c` retains its
parenthesized grouping and is a type error. Booleans support only `==`
and `!=`. Void expressions cannot be compared or used as values.
List equality/inequality compares element values recursively and requires equal
element types; list ordering is not implemented. `list[T] + list[T]` concatenates
values of the same element type. Strings/lists support integer repetition with
`sequence * count` or `count * sequence`; nonpositive counts yield an empty value.
Repeated nested values retain wood's copy-on-write isolation.

`yes if condition else no` has lower precedence than `or` and associates right.
The boolean condition evaluates first; only the selected branch runs. Branches
must have compatible nonvoid types; mixed int/float branches produce float.
Contextual collection types can determine empty branch literals.

All operands and call arguments evaluate left to right, once each. `and` and
`or` short circuit. Built-in calls, including assertion messages, evaluate their
arguments eagerly. Parentheses group expressions. Functions may be called by name
or through typed function values, including returned functions, indexed containers
and record fields. Pure call targets evaluate before arguments and retain snapshot
semantics. Stateful closures/callables use a validated rooted target path,
reacquired after arguments, like mutating methods; see [closure contracts](closures-iterators.md).
Record constructors are also called by name, with all fields supplied by position or keyword.
Indexing and error-field access bind more tightly than unary operators and can be
chained. Error values support `==` and `!=`, comparing all fields; ordering is
rejected. Dotted calls resolve imported functions, record methods, or function-valued fields.

Integer `+`, `-`, `*`, unary negation, and `//` report overflow at runtime. `/`
always converts operands to binary64 and produces a float. `//` rounds the exact
integer quotient down. `%` has the divisor's sign, and obeys `a == (a // b) * b +
a % b` mathematically. `INT64_MIN // -1` overflows; `INT64_MIN % -1` is zero.
Integer and float division by zero (including negative zero) report errors.
Float arithmetic otherwise follows IEEE 754, including infinity and NaN arising
from operations. Float overflow does not trap. Fast-math compilation is not used.
Mixed numeric operations and comparisons widen integers to binary64 first.
Bitwise operations use signed 64-bit two's-complement values. Shifts require a
count in 0..63 (R4014 otherwise). Left shift is checked multiplication by a power
of two and raises R4001 on overflow; `-1 << 63` is the minimum integer. Right
shift is arithmetic, rounding toward negative infinity. There is no unsigned
integer type or implicit wrapping arithmetic.

## Collections

```wood
values = [1, 2, 3]
copy = values
copy[-1] = 9
assert(values == [1, 2, 3])
append(copy, 4)
last = pop(copy)
matrix: list[list[int]] = [[], [1, 2]]
append(matrix[0], 7)
matrix[1][0] = 8
```

List literals infer a homogeneous element type; mixing int and float values
infers `list[float]`. Other mixed element types are rejected. `[]` requires a
contextual type from a variable annotation, existing assignment target, function
parameter/return, an append element, or a typed comparison operand. Examples:
`values: list[int] = []`, returning `[]` from a `list[int]` function, and passing
`[]` to a `list[int]` parameter. Nested literals use their contextual element type.
Named container types are invariant: a `list[int]` variable does not implicitly
convert to `list[float]`. A literal can initialize a contextual `list[float]` by
widening its integer elements.

Lists have value semantics. Assignment, parameter passing, return, and indexed
reads produce independent values. Storage is shared until mutation; mutation
detaches the affected shared storage. Nested mutation detaches each shared list
along the path. User functions receive list values; return a modified value to
give the caller the result. Ordinary wood functions have no reference parameters;
external numeric-buffer imports support the scoped borrowing rules described below.

Indices must be integers. Negative indices count back from the end; `-1` selects
the last element. Bounds checks apply to both reads and writes, including the
minimum integer index. No unchecked indexing is exposed.

`append(target, value)` and `pop(target)` mutate a named list or a nested index
path rooted in a list variable. Temporaries such as `append([1], 2)` are rejected.
Index assignment evaluates its RHS first, then target indexes from outermost to
innermost. Mutation calls evaluate target indexes before later arguments. Actual
mutable element references are resolved only after these expressions finish, so
an argument can resize a list without leaving an invalid reference. Each index is
evaluated once. A failed bounds check does not roll back earlier argument effects.

Indexed reads snapshot their base value before evaluating the index. `for` over a
list evaluates the iterable once and iterates a value snapshot. Mutating the
original list inside the body does not change the iteration sequence. Each loop
variable is a local value, and modifying a nested loop value does not modify the
iterable. `for` over a string similarly visits one-byte strings from a snapshot.

List copy is O(1); the first mutation of shared storage is O(n) in that list's
size, plus element-copy costs. Mutation of uniquely owned storage avoids that
copy. Append is amortized O(1) in unique storage; pop and checked index access
are O(1), plus element-copy costs for reads. These are complexity bounds, not timings.

## Native numerical arrays

Native numeric arrays support `a[i,j]`, elementwise arithmetic, safe strided views,
reductions and matrix multiplication. `std.num`, `std.linalg` and `std.geometry`
provide the implemented numerical/3D foundation. See [native computing](native-computing.md)
for exact shape, value, borrowing, numerical and buffer-FFI contracts.
The wood `std.geometry.read_obj` library function imports a bounded triangle-only
OBJ subset into existing mesh arrays; see [OBJ importing](obj.md). This adds no
language syntax or compiler primitive.

## Functions and control flow

```wood
fn classify(n: int) -> str:
    if n < 0:
        return "negative"
    elif n == 0:
        return "zero"
    else:
        return "positive"
```

Free functions are declared at file scope. Every ordinary parameter needs a type.
An omitted return annotation infers a result from all checked value returns;
functions without value returns remain `void`. Recursive return-dependency cycles
require an explicit annotation on at least one participating function. Annotated
functions retain recursion and mutual recursion. Functions are available throughout
the file. See [return inference](return-inference.md) for exact join rules,
concrete closure factories, generic methods and conservative limits.
Positional arguments and trailing commas are
allowed. Named calls also accept keywords and literal defaults, with `/` for
positional-only and `*` for keyword-only parameters. Overloading, variadic/unpacked
arguments are not implemented. Local named functions and owned capturing closures
are supported; see [closures and lazy iteration](closures-iterators.md). Ordinary function
parameters pass by value; JSON nodes are immutable and shared. Records can also
declare methods with implicit `self` or explicitly mutable `mut self` receivers.

See [native typed abstractions](types-and-calls.md) for the normative tuple,
optional, generic-function, and call-binding contracts, limits and examples.
See [generic records and methods](generic-records.md) for the normative record
instantiation, receiver, mutation, native layout and method-call contracts.
`fn identity[T](value: T) -> T:` declares a generic function; concrete argument
types determine cached native specializations, checked on instantiation. Tuples
use `(a,b)` literals and compile-time signed literal indexes. `T | None` stores
a native optional; use `is None` / `is not None` and `unwrap` (R4023 if absent).
`a, b = pair` and `for name, count in pairs:` unpack tuples, with the count checked
at compile time. After `if x is not None:`, in the `else` of `if x is None:`, and
after `if x is None:` whose body always returns, raises, breaks or continues, a local
`x` has its payload type without `unwrap`. Assigning `x` ends this. Tuple iteration
is not implemented.

### Function values

```wood
fn increment(value: int) -> int:
    return value + 1

fn apply(transform: fn(int) -> int, value: int) -> int:
    return transform(value)

callback = increment
assert(apply(callback, 41) == 42)
```

Own/imported functions, including qualified module members and external functions,
are values with inferred structural function types. Store them in variables,
records, lists or dictionaries, pass/return them, and call any expression of
function type. Function signatures must match exactly; numeric widening is
permitted at a call's argument boundary, not between function types. `fn() -> void`
is a valid callback type. There is no null/default function value. Builtins,
constructors and `range` are not function values; wrap a builtin in a wood
function or closure when needed. Capturing closures use `fn(x: T) => expression`
and explicit owned callable annotations use `callable(T) -> R`. A local callable takes priority over an unreserved fallback builtin;
non-callable locals retain historical builtin call resolution for compatibility.

Equality compares the identity of a declared function, including aliases to that
declaration. Ordering and JSON conversion are rejected. Distinct external
declarations have distinct wrapper identities even if they bind the same C symbol.
String formatting is `<function>`, without a memory address. These are native
function pointers, with no dynamic interpreter or Python dependency.

### Borrowed C ABI

`extern fn wood_native_add(left: int, right: int) -> int` declares a file-scope,
bodyless C-linkage function. Parameters support `int` (C `int64_t`), `float`
(C `double`), borrowed `str` (const byte pointer plus int64 length), and borrowed
synchronous scalar function pointers. Results are int/float/void. This does **not** map wood int to C int
or platform long. External symbols must be non-reserved C identifiers that are
also valid C++ identifiers. Conflicting external signatures across modules are
frontend errors. External declarations can be imported and used as function values.

Supply trusted, target-compatible static archives explicitly using repeatable
`--link-library PATH` options with run/build/test. Checking a declaration does not
verify an archive's ABI. Calling a mismatched declaration is undefined behavior;
wood cannot recover from foreign memory corruption or process termination. Do not
let exceptions cross the C ABI. Generated callbacks defer errors until foreign
return, then rethrow in wood. Bytes/callbacks are valid only during the call and
callbacks run only on the calling thread. Numeric arrays can additionally be
borrowed as contiguous `(pointer, int64_t count)` parameters. `mut name: array[T]`
is supported only on external parameters and requires a rooted mutable argument;
it detaches shared storage before the foreign call. See [native computing](native-computing.md)
for lifetime, layout, aliasing and native-library contracts. Raw pointer values,
string/array returns, records,
variadic C calls, arbitrary dynamic-library loading and automatic header binding
are unsupported. See [native-ffi.md](native-ffi.md) and [backend.md](backend.md#borrowed-c-abi).

A function sees its parameters and local variables, and can call other functions.
It cannot capture script variables. Pass data explicitly. Nonvoid functions must
return, raise, or call `exit` directly on every path. Analysis conservatively assumes loops may finish, even
`while true`; put a return after a loop when a return value is required.

Top-level non-function statements execute in source order. Declarations alone do
not call a function: a function named `main` has no special meaning. `return` is
legal only within functions. Unreachable statements following unconditional
`return`, `raise`, direct `exit`, `break`, or `continue` are compile errors.

`if`, `elif`, `else`, and `while` accept boolean conditions. `break` and `continue`
apply to the nearest enclosing loop. `for` accepts ranges, lists, arrays,
strings/dictionaries, cursors and the documented `iter`/`next` protocol.
Dictionary loops snapshot insertion-ordered keys. Range syntax is:

```wood
start = 1
stop = 6
step = 2
for i in range(stop):
    print(i)
for i in range(start, stop):
    print(i)
for i in range(start, stop, step):
    print(i)
```

Range arguments must be integers. Defaults are start zero and step one. Stop is
exclusive, the step can be negative, and a zero step is a runtime error. Arguments
evaluate once before the loop. Ranges allocate no collection and terminate safely
at integer boundaries without overflowing their internal counter.

## Expressions and collection notation

`def name(...)` introduces a function with the same typed parameters and return
rules as `fn name(...)`. `def` is contextual before a declaration name, preserving
existing identifiers named `def`. Function type annotations continue to use `fn`.

Augmented assignment supports `+= -= *= /= //= %= &= |= ^= <<= >>=` on existing
variables and rooted record/list/dict targets. It uses the corresponding checked
binary operation and must preserve the target's static type (`int /= int` is an
error; a float target is allowed). The target is read before the RHS; each index
is evaluated once. The store revalidates the captured path if RHS evaluation
resized or removed it. No mutable element reference survives RHS evaluation.
Ordinary assignment keeps its existing evaluation contract.

`value[start:stop:step]` slices a string/list. Each bound is an optional integer,
the default step is one, negative steps traverse in reverse and zero raises R4005.
Bounds clamp to the sequence; stop is exclusive. Omitted bounds differ from
explicit `-1` for a negative step. Extreme int64 bounds/steps do not overflow.
The base evaluates first, then present bounds left to right. The result has value
semantics. String slices operate on bytes. Slice assignment is unavailable.
The existing `slice(value,start,stop)` function retains its API and behavior.

```wood
squares = [i * i for i in range(10) if i % 2 == 0]
labels = {str(i): i for i in squares}
reverse = squares[::-1]
pairs = [a * 10 + b for a in range(3) for b in range(a) if b != 0]
```

Comprehensions support list elements or dictionary key/value results followed by
one or more `for name in iterable` clauses and optional boolean `if` filters.
Clauses run in written order; an inner iterable runs for each reached outer
iteration. List/string/dict iterables snapshot as in ordinary loops; direct ranges
use native cursors. Each `for` binding has its own private scope and does not leak
or reassign an outer name. Later clauses and the result can use earlier bindings.
Dictionary keys run before values; duplicates replace values while keeping their
insertion position. Comprehensions infer homogeneous result types and accept
contextual annotations, including empty nested results. Parenthesize a conditional
expression in an iterable or filter. At most 64 clauses are accepted. Async clauses,
unpacking in comprehension clauses and generator/set comprehensions are not implemented.

The separate hosted wood-written compiler implements these comprehensions for
lists, strings, dictionaries, ranges, arrays and supported custom iterators;
see its [profile](bootstrap-compiler.md). This ports
existing semantics and does not add production syntax or change language rules.

## Built-ins

| Call | Result and rules |
| --- | --- |
| `print(values...)` | Writes values separated by spaces and ends with a newline; returns void; zero arguments prints a newline |
| `eprint(values...)` | Same formatting as print, on stderr |
| `exit(code)` | Integer status 0..255; unwinds to program entry and terminates; invalid status is R4003 |
| `error(code, message)` | An `error` value with the program's own code, recorded where it is called; an invalid code is R4003 |
| `len(value)` | Byte length of a string or total element count of a list/dictionary/tuple/array, as int |
| `char_count(text)` | The number of UTF-8 characters (Unicode scalar values) in a string; invalid UTF-8 is R4013 |
| `chars(text)` | The string's characters, each as its own string, in a list |
| `char_slice(text, start, stop)` | Characters `start` to `stop`, with the offsets of `slice`: negative counts from the end, then clamped |
| `unwrap(value)` | Copies the payload of an optional; raises R4023 when absent |
| `str(value)` | String representation of a nonvoid value |
| `float(value)` | Binary64 conversion from int/float, or strict finite decimal parsing from str |
| `int(value)` | Int unchanged, float truncated toward zero, or strict signed decimal parsing from str |
| `assert(condition)` | Runtime assertion; returns void; remains enabled in optimized builds |
| `assert(condition, message)` | Assertion with a string message |
| `range(...)` | First-class bounded integer range; iterable directly or through `std.iter` |
| `append(target, value)`, `pop(target)` | Mutate a list lvalue; see collection rules |
| Text/file/argument/environment/stdin utilities | See the complete [standard library reference](stdlib.md) |

Booleans print lowercase. Floats use locale-independent shortest round-trip
formatting with `.0` added for finite integral values that have no exponent.
Scientific notation may be used. Strings print their bytes directly. wood does
not change a Windows console's text encoding; configure a UTF-8 terminal for
non-ASCII text.

## Application data

Records, dictionaries, membership, and JSON are specified in
[data-model.md](data-model.md), including constructors, contextual literals,
recursive data, key restrictions, copy isolation, iteration, equality and costs.
`slice(value, start, stop)` returns a string/list value between clamped offsets;
negative offsets count from the end. Stop is exclusive; reversed bounds return
empty. Integer extremes are safe. Slice notation additionally supports omitted bounds and steps.
String slicing operates on bytes and can split UTF-8 sequences; `char_slice` slices by character.

## Modules and errors

`wood init [directory]` creates the existing format-1 project layout with
`src/main.wd`, a reusable greeting module and a native test. Omit the directory or
use `.` for an empty current directory. This changes no language syntax or module
rules; see [project initialization and safety](projects.md).

File-scope `import std.stats as stats` makes `stats.mean(values)` available.
`from helpers import compute as run` imports a named function. Modules export
their own functions and records without a leading underscore; imports are not re-exported.
Imported files contain only functions, records and imports. Relative imports are explicit
(`from .helpers import compute`); module cycles and root escapes are errors.
Project manifests scope local dependency aliases. See [projects.md](projects.md)
for resolution rules, package boundaries, and tooling contracts.

```wood
from std.stats import mean
try:
    print(mean([]))
except err:
    eprint(err.code, err.message)
```

`except name` catches wood runtime errors in the corresponding `try` suite. Its
binding has type `error`, with read-only fields `code`, `message`, `file` (str),
`line`, `column` (int). It is local to the handler. Each suite has its own lexical
scope. `raise "message"` creates R4010; `raise err` preserves the original fields,
and a bare `raise` inside an `except` block raises the error being handled.
`error("app.not_found", "message")` makes an error with the program's own code: 1 to 64
ASCII letters, digits, `.`, `_` or `-`, starting with a letter, and not shaped like
wood's own codes (`R4010`). A `finally:` block, after the handler or straight after
the try body, runs however the statement is left: normal completion, `return`,
`break`, `continue` or an error, which it raises again afterwards. It cannot itself
`return`, `break` or `continue` out of the statement (E3011, E3012), and `exit` skips it.
Control-flow analysis requires the handler to satisfy the same return obligations
as the try body. It does not infer that particular operations cannot fail. Mutations
before failure are retained. `exit` is not caught. There is no exception hierarchy.
`finally` is a keyword only in this position, so it remains a valid name. See
[errors.md](errors.md) for examples and exact boundaries.

## Scoped native resources

```wood
import std.io as io
with io.open("input.bin", "r") as source:
    with io.open("output.bin", "x") as destination:
        io.copy(source, destination, 1073741824)
```

`with expression as name:` evaluates one native resource expression (`stream`,
`database`, `statement`, `transaction`, `tls_context` or `task`), creates a fresh
block-local binding (which may shadow an outer variable), and closes the original
handle when the block leaves. This covers normal completion, return, break,
continue, raised errors and `exit`. Nested scopes close in reverse order. Assigning
another handle to the binding does not change which resource the scope closes.
The word `with` is contextual in this statement; existing variables/functions
named `with` continue to work. There are no user-defined context managers yet.

Native resource copies share one identity, including copies inside
records, lists/dictionaries, parameters and returns. Closing any alias invalidates
all aliases; using a closed stream raises R4016. Other resource errors are documented
in [native backend applications](backend.md). Equality compares identity;
ordering, JSON conversion and foreign C ABI parameters are rejected. Printing
produces opaque type/state labels (for example `<stream:open>` or `<stream:closed>`).
Container/record copies still have
value semantics, but copying a handle never duplicates an OS resource or cursor.

Explicit close is idempotent. The final handle also releases the resource if no
scope/explicit close was used, but fallback destruction cannot report close/flush
errors. A scope reports close errors on normal completion and return/loop control.
While unwinding another error or exit, it preserves the primary failure and
suppresses secondary close errors. No rollback or crash durability is implied;
forced process termination does not guarantee cleanup. Check/flush important
output explicitly before a possible failure. Transaction scopes roll back unless
explicitly committed; task scopes cancel and join. Tasks exchange immutable JSON;
socket handoff invalidates the sender's aliases. Socket waits/retries observe
cooperative cancellation with R4022, while synchronous DNS retains the OS resolver
policy. Cancellation does not forcibly preempt code. See [streams and networking](networking.md)
and [native databases, TLS and tasks](backend.md) for lifetimes, limits and dependencies.

## Current boundaries

`std.path` includes canonical, link-aware `resolve(path, allow_missing=false)`,
`identity(path)`, `within(path, root)` and the host syntax predicate
`is_absolute(path)`. They preserve normal static argument checking and report
filesystem failures as R4008. Containment checks are not a race-free sandbox.
See [filesystem and path contracts](stdlib.md#native-filesystem-and-paths).

`std.time` supplies blocking sleep and steady-clock seconds/integer nanosecond
counters, including `perf_counter` APIs for elapsed measurements. Sleep accepts
finite seconds in 0..86400; invalid durations raise R4015. Counter epochs are
process-local and clock precision is platform-dependent. See [native time](stdlib.md#native-time).

Frontend failures are diagnosed before invoking C++. Runtime errors include the
wood source file, line, column, and an `R` code. Exceptions from the C++ standard
library (such as allocation failure) produce `R4099`. Public `print` and `eprint`
write synchronously and raise catchable `R4008` with message
`could not write to output stream` and the original call location on failure.
Earlier bytes may already have been written; output is not transactional.
Linux broken pipes are handled without changing the process-wide SIGPIPE policy.
Resource exhaustion
and stack overflow are not guaranteed to be recoverable. wood is not a sandbox.

Slice assignment, classes and inheritance, generic constraints, bound method values, pattern matching, async syntax and
event loops, C bindings with raw pointers or structs, database drivers other than SQLite, version ranges, a REPL and a
formatter aren't available. See [editors](editors.md) for the language server, [the CLI](cli.md) for diagnostics, and
[the roadmap](roadmap.md) for plans.

Compiling and running wood never needs Python. The tests that compare against Python are optional, and
`-DWOOD_PYTHON_TESTS=OFF` turns them off. Standard modules are written in wood; byte primitives and OS support are C++ or C.
The [Python bridge](python-distribution.md) needs CPython 3.14 only when a program uses it.

## Owned closures and lazy iteration

The normative [closure and iterator contracts](closures-iterators.md) define expression
closures, local named functions, owned immutable/mutable captures, callable patterns,
associated types and the `iter`/`next` protocol. `std.iter` supplies lazy adapters and
short-circuiting consumers in native wood. `for` and comprehensions accept these cursors.
`range(...)` is a first-class value; direct range loops preserve their existing behavior.

Parentheses group type annotations, for example `(callable(int) -> int) | None`
is an optional callable, while `callable(int) -> (int | None)` returns an optional.
