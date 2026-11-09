# Learn wood: from a script to a tested application

Follow [installation](installation.md), then save this as `greeting.wd`.
Each wood block below is a complete program you can save and run independently.
Later sections build on the ideas without requiring hidden session state.

```wood
name = "Wood"
print("hello, " + name)
```

Run `wood greeting.wd`. The result is `hello, Wood`. The compiler infers `name`
as `str`. You can assign another string to it; assigning a number is a type error.

## Functions and numbers

```wood
fn square(value: int) -> int:
    return value * value

print(square(12))
ratio = 5 / 2
assert(ratio == 2.5)
```

Function parameters have explicit types. `-> int` declares the return type.
Without that annotation, wood infers the result from checked returns; a function
without value returns has type `void`. Recursive return dependencies can require
an explicit annotation. See [return inference](return-inference.md). Functions can
call functions declared later in the file. Locals infer their type on their first
assignment.

`def` is also accepted for typed function declarations. `True` and `False` work
alongside the existing lowercase spellings when no explicit binding shadows them.

Native expressions make collection transformations concise:

```wood
fn square(value: int) -> int:
    return value * value

squares = [square(i) for i in range(8) if 1 <= i < 6]
selected = squares[::-1]
selected += [99]
label = "many" if len(selected) > 3 else "few"
lookup = {str(value): value for value in selected}
print(label, lookup)
```

Comprehension variables stay local to the comprehension. Lists keep value
semantics, including slices and repeated nested lists. Strings slice bytes;
these idioms do not imply Python object or Unicode semantics. See the
[compatibility direction](python-compatibility.md) and [sales report](../examples/sales_report.wd).

`int` is a signed 64-bit number. Arithmetic reports overflow instead of silently
wrapping. `/` always produces a `float`; `//` performs integer floor division.
Use `float(3)` to convert explicitly, or declare `value: float = 3` to widen an
integer initializer. Narrowing requires `int(value)` and truncates toward zero.

## Conditions and repetition

```wood
total = 0
for n in range(1, 6):
    total = total + n

if total == 15:
    print("sum:", total)
else:
    print("unexpected result")
```

Indent blocks consistently with spaces. Four spaces are conventional. A range
excludes its stop value and can have a third, nonzero step argument. `break`
exits the nearest loop and `continue` skips to its next iteration.

Conditions must be `bool`. Write `if count != 0:`, not `if count:`. Boolean
operators are `and`, `or`, and `not`. `and` and `or` short circuit.

An assignment changes an existing variable if it is visible. Otherwise it
introduces a variable in the current block. To accumulate across a loop or
branch, initialize the variable before that block. The range variable is local
to its loop and does not change a same-named outer variable.

## Build and inspect

`wood check greeting.wd` checks the program without running it. `wood build
greeting.wd -o greeting` creates a native executable (use `greeting.exe` on Windows).
`wood emit-cpp greeting.wd -o greeting.cpp` shows the full native translation,
including its runtime. An installed compiler does not need this repository's
runtime headers.

Try [fibonacci](../examples/fibonacci.wd), [primes](../examples/primes.wd), and
[control flow](../examples/control_flow.wd). The [language reference](language.md)
specifies the complete implemented feature set and its current limits.

## Collections and command-line tools

```wood
numbers: list[int] = []
for text in args():
    append(numbers, int(text))
total = 0
for number in numbers:
    total = total + number
print("sum:", total)
```

Save this as `sum.wd` and run `wood sum.wd -- 10 20 -6`; it prints `sum: 24`.
Lists are independent values, with storage shared until mutation. Their indexes
are checked, and `-1` selects the final element. `[]` needs a contextual type.
Function parameters are values too: return a changed list to update the caller.

Use `read_text(path)`, `split(text, "\n")`, `strip(text)`, and strict `int(text)` to
read a simple data file. `write_text(path, content)` writes bytes without newline
conversion. The [text report example](../examples/text_report.wd) ties these
pieces together. See the [standard library](stdlib.md) for all supported calls.

## Reuse and recovery

```wood
from std.collections import sorted_int
from std.stats import sum_int

numbers: list[int] = []
try:
    for text in args():
        append(numbers, int(text))
    print(sorted_int(numbers), sum_int(numbers))
except err:
    eprint(err.code, err.message)
    exit(1)
```

Imported functions are checked with your program and compiled into the executable.
The handler catches invalid numeric input and checked runtime errors. It receives
an error value with the original file, line, and column too. Use `raise err` to
propagate it or `raise "message"` for an application error.

Create a project with `wood init hello`, then `wood test hello` to compile and run
its sample test. Put reusable functions in source modules and import them by name.
Use `wood lock hello`, then `wood test hello --locked --report=json` for a checked
dependency inventory and structured test results. [Projects and packages](projects.md)
walks through manifests and explicit local dependencies; [errors.md](errors.md)
specifies handler scopes and recovery boundaries.

## Records, generics and explicit mutation

Records group typed values. A read method receives a value; a `mut self` method
updates a rooted caller record. Copying the record preserves its previous value.
Type arguments usually infer from constructor/call arguments:

```wood
record Box[T]:
    value: T
    fn read(self) -> T:
        return self.value
    fn replace(mut self, value: T):
        self.value = value

box = Box([1, 2])
saved = box
box.replace([3, 4])
assert(saved.read() == [1, 2])
assert(box.read() == [3, 4])
```

There is no inheritance, field default or escaping reference. Use composition
and small functions. See [generic records](generic-records.md) for constructor
context, methods with additional type parameters and safe nested mutation.

## Optional results and flexible calls

Tuples return a fixed set of differently typed values. An optional makes absence
explicit; test it, then `unwrap` to access its payload. Literal defaults, positional
`/` and keyword-only `*` modes make ordinary named calls concise:

```wood
fn describe(code: int, /, label: str = "item", *, count: int = 1) -> tuple[str,int]:
    return (label + ":" + str(code), count)

fn first(items: list[int]) -> int | None:
    if len(items) == 0:
        return None
    return items[0]

row = describe(7, count=2)
assert(row == ("item:7", 2))
selected = first([9, 10])
if selected is not None:
    assert(unwrap(selected) == 9)
assert(first([]) is None)
```

Tuples use literal indices such as `row[0]`; they do not unpack. Other union
types, implicit optional narrowing and argument unpacking are unavailable.

## Closures and lazy pipelines

Expression closures own their captures. Generic iterator adapters keep concrete
types and do not create intermediate result lists; collection is explicit:

```wood
import std.iter as it

factor = 3
transform = fn(value: int) => value * factor
factor = 99
assert(transform(2) == 6)
pipeline = it.take(it.map(it.filter(range(100), fn(x) => x % 2 == 0), transform), 4)
assert(it.collect(pipeline) == [0, 6, 12, 18])
assert(it.fold([1, 2, 3], 0, fn(total: int, item: int) => total + item) == 6)
```

Use `[mut name]` only when a closure needs its own mutable captured state.
It does not mutate the outside variable. Cursor copies preserve value snapshots;
stream-backed adapters share the underlying stream identity. Read
[closure and iterator contracts](closures-iterators.md) before retaining state.

## Validated data and scoped files

This example serializes data, writes it through a resource scope, then reads it
with a byte budget. Save/run in a scratch directory: `report.csv` is truncated.

```wood
import std.csv as csv
import std.io as io

rows = [["name", "quantity"], ["book, blue", "2"], ["pen", "3"]]
text = csv.dump(rows, ",")
assert(csv.parse(text, ",") == rows)
with io.open("report.csv", "w") as destination:
    io.write(destination, text)
table: list[dict[str, str]] = []
with io.open("report.csv", "r") as source:
    table = csv.read_table(io.read_all(source, 65536), ",")
assert(int(table[0]["quantity"]) + int(table[1]["quantity"]) == 5)
print(json_dump(table))
```

Use `x` mode for exclusive creation when overwriting is not intended. Resources
close even when wood raises an error; copies of a stream share its identity.
CSV/JSON whole-document functions still use memory proportional to their data.

## Native arrays and geometry

Arrays are fixed-shape numeric values. A transpose is an owned snapshot, not a
mutable alias. Shapes and indices are checked; no Python or numerical package
is needed:

```wood
import std.num as num
import std.linalg as la

points = num.reshape(num.from_list([1.0, 2.0, 3.0, 4.0, 5.0, 6.0]), (2, 3))
saved = num.transpose(points)
points[0, 0] = 9.0
assert(saved[0, 0] == 1.0)
moved = la.transform_points(points, la.translation(1.0, 0.0, -1.0))
assert(moved[1, 0] == 5.0 and moved[1, 2] == 5.0)
```

Try the complete [OBJ load/transform/export application](../examples/mesh_import.wd):

```sh
wood examples/mesh_import.wd -- tests/fixtures/obj/tetrahedron.obj transformed.obj
```

Run from the source/manual tree containing those paths. Only the
[documented triangle OBJ subset](obj.md#accepted-format) is accepted. Numerical
limits and floating-point policies live in [native computing](native-computing.md).

## Finish with a project

Use the generated project before adding dependencies:

```sh
wood init hello
wood check hello
wood lock hello
wood test hello --locked --report=json
wood run hello --locked -- world
wood build hello --locked -o hello-app
```

Run `./hello-app world` on Linux or `.\hello-app.exe world` on Windows. The generated greeting tests and the actual executable
should agree. Read [projects](projects.md) to add local packages and
[the CLI](cli.md) to bound child processes. Use [the examples index](examples.md)
for larger applications and [the complete API index](stdlib-api.md) to find
the functions that exist. Don't assume a function from another language is there.
