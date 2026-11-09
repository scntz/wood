# Error explanations

`wood explain CODE` prints these sections, and every example here is run by `tests/explanations.cmake`. A code keeps its meaning, while the wording of a message can improve.
Codes starting with `E` come from the compiler, `R` from a running program and `D` from the `wood` command.

## E1001 - Tab in indentation

Meaning:
A tab character appears outside a string.

Why it happens:
wood blocks are formed by indentation, and mixing tabs and spaces hides how deep a
line really is. So tabs are refused everywhere except inside strings.

How to fix:
Indent with spaces. Most editors can convert tabs to spaces for the whole file.

Example that fails:
```wood
if true:
	print("indented with a tab")
```

One correction:
```wood
if true:
    print("indented with spaces")
```

## E1002 - Inconsistent indentation

Meaning:
A line's indentation does not match any enclosing block.

Why it happens:
When a block ends, the next line must return to exactly the indentation of an outer
block. This can also mean blocks are nested more than 255 levels deep.

How to fix:
Line the statement up with the block it belongs to.

Example that fails:
```wood
if true:
    print("inside")
  print("neither inside nor outside")
```

One correction:
```wood
if true:
    print("inside")
print("outside")
```

## E1003 - Malformed number

Meaning:
A numeric literal is incomplete or is followed by letters.

Why it happens:
A decimal point needs digits after it, an exponent needs digits, and a number cannot
run straight into a name.

How to fix:
Finish the number, or put an operator or space between the number and the name.

Example that fails:
```wood
price = 3.
print(price)
```

One correction:
```wood
price = 3.0
print(price)
```

## E1004 - Unterminated string

Meaning:
A string has no closing quote on its line.

Why it happens:
A closing quote is missing, or a backslash at the end of the text escapes the quote
that was meant to close it.

How to fix:
Add the closing quote. To put a line break in a string, write `\n`.

Example that fails:
```wood
print("hello)
```

One correction:
```wood
print("hello")
```

## E1005 - Invalid string contents

Meaning:
A string holds an escape wood does not know, or a raw control character.

Why it happens:
Windows paths are a common cause, since `\U` in `"C:\Users"` reads as an escape.

How to fix:
Double a literal backslash, or use forward slashes in paths.

Example that fails:
```wood
print("C:\Users")
```

One correction:
```wood
print("C:/Users")
```

## E1006 - Unbalanced brackets

Meaning:
A parenthesis or bracket is not closed, closes the wrong thing, or is nested too deeply.

Why it happens:
Brackets must pair up in order. Nesting is limited to 256 levels.

How to fix:
Find the opening bracket named in the message and close it.

Example that fails:
```wood
print((1 + 2)
```

One correction:
```wood
print((1 + 2))
```

## E1007 - Unexpected character

Meaning:
A character appears that cannot begin any part of a wood program.

Why it happens:
Text was pasted from elsewhere, or a symbol from another language was used. wood has
no `$`, `?` or `;`, and `@` appears only in imports such as `import python@numpy`.

How to fix:
Remove the character, or rewrite the expression in wood.

Example that fails:
```wood
total = 1 $ 2
```

One correction:
```wood
total = 1 + 2
print(total)
```

## E2001 - Syntax error

Meaning:
The parser expected something else at this position.

Why it happens:
Common causes are a missing `:` before a block, `else if` rather than `elif`, or `=`
where `==` was meant. The message names what was expected.

How to fix:
Follow the message, and check the end of the previous line as well as this one.

Example that fails:
```wood
if 1 < 2
    print("yes")
```

One correction:
```wood
if 1 < 2:
    print("yes")
```

## E2002 - Unsupported form

Meaning:
This construct is outside what the language accepts, or goes past a fixed limit.

Why it happens:
The message gives the rule. Limits include 64 levels of type nesting, 64 levels of
closures and 255 captures. A few forms from other languages are refused here too.

How to fix:
Rewrite it in the form the message describes, or split it into smaller named pieces.

## E2003 - Invalid type

Meaning:
A type annotation does not name a type wood accepts here.

Why it happens:
The type may be misspelled. Only `T | None` unions are supported, so `int | str` is
not a type, and `void` can only be a function's return type.

How to fix:
Use a known type. For a value of several kinds, use a record or `json`.

Example that fails:
```wood
value: int | str = 1
print(value)
```

One correction:
```wood
value: int | None = 1
print(value)
```

## E2004 - Expected an expression

Meaning:
A value is needed here, and what follows cannot be one.

Why it happens:
An operator is missing its right side, or `not` is used as an operand without
parentheses.

How to fix:
Complete the expression, and parenthesize `not` inside larger expressions.

Example that fails:
```wood
total = 1 +
print(total)
```

One correction:
```wood
total = 1 + 2
print(total)
```

## E2006 - External function in the wrong place

Meaning:
An `extern fn` is declared inside a function or block.

Why it happens:
External functions are part of a file's interface with native code, so they exist at
file scope only.

How to fix:
Move the declaration to the top level of the file. See docs/native-ffi.md.

## E2010 - Invalid assignment target

Meaning:
The left side of `=` cannot be assigned to.

Why it happens:
Only variables, list items and fields can be assigned. A call or literal cannot.

How to fix:
Assign to a variable. If `==` was meant, write `==`.

Example that fails:
```wood
fn value() -> int:
    return 1
value() = 2
```

One correction:
```wood
fn value() -> int:
    return 1
total = value()
print(total)
```

## E2011 - Import in the wrong place

Meaning:
An import is inside a function or block, or uses a provider wood does not support.

Why it happens:
Imports are resolved before a program runs, so they belong at file scope. The only
provider form is `import python@module`.

How to fix:
Move the import to the top level of the file.

Example that fails:
```wood
fn work() -> float:
    import std.math as math
    return math.sqrt(4.0)
print(work())
```

One correction:
```wood
import std.math as math
fn work() -> float:
    return math.sqrt(4.0)
print(work())
```

## E2012 - Record in the wrong place

Meaning:
A `record` is declared inside a function or block.

Why it happens:
Records are types, and a type's name must be known throughout its file.

How to fix:
Move the record to the top level of the file.

Example that fails:
```wood
fn make() -> int:
    record Point:
        x: int
    return Point(1).x
print(make())
```

One correction:
```wood
record Point:
    x: int
fn make() -> int:
    return Point(1).x
print(make())
```

## E3001 - Duplicate declaration

Meaning:
A name is declared twice where it must be unique.

Why it happens:
Two functions, records, parameters or fields share a name. wood has no overloading, so
a second function with the same name is an error even when its parameters differ.

How to fix:
Rename one of them.

Example that fails:
```wood
fn show(value: int) -> int:
    return value
fn show(value: str) -> str:
    return value
print(show(1))
```

One correction:
```wood
fn show_int(value: int) -> int:
    return value
fn show_str(value: str) -> str:
    return value
print(show_int(1))
```

## E3002 - Type mismatch or invalid operand

Meaning:
An expression does not have the type required at this location, or an
operation does not support its operand types.

Why it happens:
Variables keep their types after initialization. Wood does not implicitly
convert strings to numbers or numbers to bool. Container element types
must agree; int-to-float widening does not convert list[int] to list[float].

How to fix:
Read the expected and actual types in the diagnostic. Supply a compatible
value, choose the intended annotation, or convert explicitly when appropriate.
Text conversion with int/float can fail at runtime if the text is invalid.
For conditions, use a bool expression such as count != 0.

Example that fails:
```wood
count: int = "3"
```

One correction:
```wood
count: int = int("3")
assert(count == 3)
```

## E3003 - Incorrect function arguments

Meaning:
A call's arguments do not match the callable's parameter rules.

Why it happens:
A required argument may be missing, too many positional arguments may be
supplied, or a keyword may be unknown, duplicated or positional-only.
Some callable forms accept positional arguments only.

How to fix:
Follow the expected signature or count in the diagnostic. Supply required
arguments and remove extras or duplicates. Parameters before / must be
positional; parameters after * must use keywords. Only parameters with
defaults can be omitted.

Example that fails:
```wood
fn greet(name: str) -> str:
    return "hello, " + name
print(greet())
```

One correction:
```wood
fn greet(name: str) -> str:
    return "hello, " + name
assert(greet("wood") == "hello, wood")
```

## E3005 - Unknown function

Meaning:
The compiler cannot resolve this function or constructor name at the call site.

Why it happens:
The name may be misspelled, not imported, or missing its module alias.
Wood resolves calls statically; it does not search for or import a function
automatically.

How to fix:
Check the spelling and any suggested name. Declare the callable in this file
or import it; file-scope functions can be called before their definitions.
After import helpers as tools, use tools.function_name(...).
A suggestion is advice, not an automatic correction.

Example that fails:
```wood
fn greet(name: str) -> str:
    return "hello, " + name
gret("wood")
```

One correction:
```wood
fn greet(name: str) -> str:
    return "hello, " + name
assert(greet("wood") == "hello, wood")
```

## E3006 - Number out of range

Meaning:
A numeric literal does not fit its type.

Why it happens:
Integers are signed 64-bit, and floats are finite binary64.

How to fix:
Use a smaller value. For larger integers, keep the digits in a string or use a float,
accepting its limited precision.

Example that fails:
```wood
big = 99999999999999999999
print(big)
```

One correction:
```wood
big = 9999999999999999
print(big)
```

## E3007 - Undefined variable

Meaning:
This name is not available as a value in the current lexical scope.

Why it happens:
A variable may be misspelled, used before its first assignment, or declared
only inside a branch or loop. File-scope functions do not capture script
variables; local closures have separate, documented capture rules.

How to fix:
Check spelling and scope. Initialize shared values in the enclosing block
before a branch, and pass needed values as function parameters. Do not rely
on a branch or loop binding being available after that block.

Example that fails:
```wood
if true:
    total = 1
print(total)
```

One correction:
```wood
total = 0
if true:
    total = 1
assert(total == 1)
```

## E3008 - Unreachable statement

Meaning:
A statement can never run.

Why it happens:
It follows a `return`, `raise`, `break` or `continue` in the same block.

How to fix:
Remove it, or move it before the statement that ends the block.

Example that fails:
```wood
fn value() -> int:
    return 1
    print("never")
print(value())
```

One correction:
```wood
fn value() -> int:
    print("runs first")
    return 1
print(value())
```

## E3009 - Annotation on a later assignment

Meaning:
A variable that already exists is given a type annotation again.

Why it happens:
A variable's type is fixed at its first assignment, so only that one can be annotated.

How to fix:
Remove the annotation, or use a new name if a different type was intended.

Example that fails:
```wood
count = 1
count: int = 2
print(count)
```

One correction:
```wood
count = 1
count = 2
print(count)
```

## E3010 - Cannot iterate over this value

Meaning:
The value in a `for` cannot be iterated.

Why it happens:
A `for` accepts a list, dictionary, string, range, or a value with an iterator protocol.
An integer is none of these, so counting needs `range`.

How to fix:
Iterate over `range(n)` to count, or add the protocol. See docs/closures-iterators.md.

Example that fails:
```wood
for item in 5:
    print(item)
```

One correction:
```wood
for item in range(5):
    print(item)
```

## E3011 - Return outside a function

Meaning:
`return` appears at file scope.

Why it happens:
There is no function to return from. A script's top level simply runs to its end.

How to fix:
Move the code into a function, or use `exit(code)` to end the program early.

Example that fails:
```wood
return 1
```

One correction:
```wood
exit(0)
```

## E3012 - Break or continue outside a loop

Meaning:
`break` or `continue` is not inside a `for` or `while`.

Why it happens:
A function called from a loop is not part of that loop, so its body cannot break it.

How to fix:
Move the statement into the loop, or return a value and let the loop decide.

Example that fails:
```wood
if true:
    break
```

One correction:
```wood
while true:
    break
print("done")
```

## E3013 - Missing return path

Meaning:
A function that produces a value can reach the end of its body without one.

Why it happens:
A branch may fall through, or the only return may be inside a loop.
Return analysis conservatively allows loops to finish, including while true.
Wood does not insert an implicit None result.

How to fix:
Return a compatible value or raise on every possible path. A direct exit
also terminates a path. If absence is intentional, use T | None and return
None explicitly. An annotation alone does not add a missing return.

Example that fails:
```wood
fn absolute(value: int) -> int:
    if value < 0:
        return -value
```

One correction:
```wood
fn absolute(value: int) -> int:
    if value < 0:
        return -value
    return value
assert(absolute(-3) == 3 and absolute(3) == 3)
```

## E3014 - Empty collection needs a type

Meaning:
The compiler has no concrete element type for [] or key/value types for {}.

Why it happens:
An empty literal contains no values from which to infer its types, and no
suitable annotation or call context supplies them. A later append does not
retroactively type an earlier unannotated empty assignment.

How to fix:
Provide an annotation such as list[int] or dict[str, int]. A concrete
parameter or return annotation can also supply context for an empty literal.

Example that fails:
```wood
values = []
```

One correction:
```wood
values: list[int] = []
append(values, 1)
assert(values == [1])
```

## E3015 - Invalid mutation target

Meaning:
A method that changes its receiver is called on something that is not a stored value.

Why it happens:
A mutation needs somewhere to keep its result, so its target must be a variable or a
path that starts at one. A value returned by a call is a temporary.

How to fix:
Store the value in a variable, then change the variable.

Example that fails:
```wood
fn items() -> list[int]:
    return [1]
append(items(), 2)
```

One correction:
```wood
fn items() -> list[int]:
    return [1]
values = items()
append(values, 2)
print(values)
```

## E3016 - Unknown field or method

Meaning:
A value has no member with this name.

Why it happens:
The name may be misspelled, or it may belong to a different type. The message lists
the available names when it can.

How to fix:
Use a listed name, or check the value's type.

Example that fails:
```wood
record Point:
    x: int
print(Point(1).y)
```

One correction:
```wood
record Point:
    x: int
print(Point(1).x)
```

## E3017 - Invalid raise

Meaning:
`raise` is given something other than a string or an error.

Why it happens:
wood has a single error type and no exception classes, so other values cannot be raised.

A bare `raise` re-raises the error being handled, so it is only allowed in an `except` block.

How to fix:
Raise a string, convert the value with `str`, make one with `error(code, message)`, or re-raise a caught error.

Example that fails:
```wood
raise 42
```

One correction:
```wood
try:
    raise str(42)
except err:
    print(err.message)
```

## E3020 - Invalid dictionary key type

Meaning:
A dictionary's key type is not `int`, `str` or `bool`.

Why it happens:
Keys need exact equality and a stable order, so other types are not allowed. Floats
are ruled out because of NaN and rounding.

How to fix:
Build a string or integer key from the value.

Example that fails:
```wood
scores: dict[float,str] = {}
print(len(scores))
```

One correction:
```wood
scores: dict[str,str] = {}
print(len(scores))
```

## E3021 - Record contains itself

Meaning:
A record has a field of its own type, directly or through other records.

Why it happens:
Records are stored by value, so such a record would have infinite size.

How to fix:
Hold the inner value in a list or dictionary, which may be empty.

Example that fails:
```wood
record Node:
    value: int
    next: Node
print("unused")
```

One correction:
```wood
record Node:
    value: int
    next: list[Node]
print(Node(1, []).value)
```

## E3022 - Cannot convert to JSON

Meaning:
This type has no JSON form.

Why it happens:
JSON conversion supports scalars, lists, tuples, records and dictionaries with string
keys. A dictionary with other keys, or a function, has no equivalent.

How to fix:
Convert the value to one of those shapes first.

## E3023 - Invalid external declaration

Meaning:
An `extern fn` or its use breaks the rules for calling native code.

Why it happens:
External functions cannot be generic, two modules cannot declare one differently, and
`mut` parameters are limited to native array imports called by name.

How to fix:
Follow the message, and see docs/native-ffi.md.

## E3024 - Invalid generic use

Meaning:
A generic function or record cannot be resolved to concrete types.

Why it happens:
A type parameter may not appear in any argument, or the wrong number of type arguments
is given, so the compiler has nothing to infer it from.

How to fix:
Annotate the result or give the type arguments. See docs/generic-records.md.

## E3025 - Invalid tuple index

Meaning:
A tuple is indexed in a way the compiler cannot check.

Why it happens:
A tuple's items can have different types, so its index must be an integer literal, and
that literal must be in range.

How to fix:
Use a literal in range. For a computed index, use a list.

Example that fails:
```wood
pair = (1, "two")
print(pair[2])
```

One correction:
```wood
pair = (1, "two")
print(pair[1])
```

## E3026 - Invalid default argument

Meaning:
A parameter's default is not a literal.

Why it happens:
Defaults are evaluated once for every call, so only literals, containers of literals
and unary literal expressions are allowed. This keeps calls free of hidden state.

How to fix:
Use a literal default, or pass the value at each call.

Example that fails:
```wood
fn start() -> int:
    return 1
fn count(first: int = start()) -> int:
    return first
print(count())
```

One correction:
```wood
fn count(first: int = 1) -> int:
    return first
print(count())
```

## E3027 - Invalid method receiver

Meaning:
A method's `self` is missing, or it changes its receiver without `mut self`.

Why it happens:
Methods state whether they change the record. A method declared with `self` is
read-only, and a method declared without `self` cannot be called on a value.

How to fix:
Declare `mut self` to change the record.

Example that fails:
```wood
record Counter:
    total: int
    fn add(self):
        self.total = self.total + 1
c = Counter(0)
c.add()
```

One correction:
```wood
record Counter:
    total: int
    fn add(mut self):
        self.total = self.total + 1
c = Counter(0)
c.add()
print(c.total)
```

## E3028 - Invalid closure

Meaning:
A closure's parameters or captures break a closure rule.

Why it happens:
Captured values are immutable snapshots, so a closure cannot change them. A closure
parameter also needs a type, unless the call it is passed to supplies one.

How to fix:
Annotate the parameter, or return the new value rather than changing a capture. See
docs/closures-iterators.md.

## E3029 - Invalid iterator protocol

Meaning:
A type's iterator does not follow the protocol, or an iterator tool is given the wrong kind of value.

Why it happens:
An iterable needs `fn iter(self)`, a cursor needs `fn next(mut self)` returning `step[T]`, and neither takes other parameters.

How to fix:
Match those signatures. See docs/closures-iterators.md.

Example that fails:
```wood
record Count:
    left: int
    fn next(self) -> step[int]:
        return finished()
for item in Count(1):
    print(item)
```

One correction:
```wood
record Count:
    left: int
    fn next(mut self) -> step[int]:
        if self.left == 0:
            return finished()
        self.left = self.left - 1
        return yielded(self.left)
for item in Count(1):
    print(item)
```

## E3030 - Return type cannot be inferred safely

Meaning:
The compiler cannot establish one compatible result type for an unannotated
function. Most nonrecursive functions do not need a return annotation.

Why it happens:
Return inference may depend recursively on itself, directly or through
other functions. Returns may also have incompatible types, or mix a bare
return with value returns. Separate closure expressions have distinct types.

How to fix:
For a recursive dependency, annotate at least one function in the cycle.
For inconsistent returns, make the returned values compatible; an annotation
does not make int and str interchangeable. For an absent value, use T | None
and return None, not a bare return. Compatible closures can share an explicit
callable(...) -> R result. All value-returning paths must still return, raise
or directly exit (see E3013). The example fixes recursion, not every cause.

Example that fails:
```wood
fn countdown(n: int):
    if n == 0:
        return 0
    return countdown(n - 1)
```

One correction:
```wood
fn countdown(n: int) -> int:
    if n == 0:
        return 0
    return countdown(n - 1)
assert(countdown(3) == 0)
```

## E3031 - Invalid array

Meaning:
A native array has an unsupported element type or shape.

Why it happens:
Array elements must be `int` or `float`. A shape is an `int`, a tuple of rank 1 to 8,
or a `list[int]`.

How to fix:
Use a list for other element types. See docs/native-computing.md.

## E3100 - Import without a file

Meaning:
A program with imports was compiled with no file path to resolve them from.

Why it happens:
Relative imports are found from the importing file's location.

How to fix:
Compile from a file.

## E3101 - Module not found

Meaning:
An import names a module that cannot be found.

Why it happens:
The name may be misspelled, or the file is not where it is expected. Standard modules
are named `std.something`. Package imports must be listed in `wood.json`.

How to fix:
Check the spelling and location. For a package, run `wood add`.

Example that fails:
```wood
import std.maths as math
print(math.sqrt(4.0))
```

One correction:
```wood
import std.math as math
print(math.sqrt(4.0))
```

## E3102 - Cyclic import

Meaning:
Modules import each other in a loop, and the message shows the chain.

Why it happens:
wood resolves modules in order, so a module cannot depend on itself through others.

How to fix:
Move what both modules need into a third module that they each import.

## E3103 - Module with top-level code

Meaning:
An imported module contains something other than functions, records and imports.

Why it happens:
Importing must not run code, so that a module has no hidden effects.

How to fix:
Move the statements into a function, and call it from the main program.

## E3104 - Name not exported by module

Meaning:
A `from module import name` asks for something the module does not define.

Why it happens:
The name may be misspelled, or defined in another module.

How to fix:
Check the module's functions. `wood inspect` lists a project's modules.

Example that fails:
```wood
from std.math import squareroot
print(squareroot(4.0))
```

One correction:
```wood
from std.math import sqrt
print(sqrt(4.0))
```

## E3105 - Conflicting import name

Meaning:
An import would give a name that is already taken, or a reserved one.

Why it happens:
Two imports use the same alias, or an alias matches a built-in name.

How to fix:
Choose another alias with `as`.

Example that fails:
```wood
import std.math as tools
import std.stats as tools
print(tools.mean([1.0]))
```

One correction:
```wood
import std.math as math
import std.stats as stats
print(stats.mean([1.0]))
```

## E3106 - Module outside the project

Meaning:
A module is outside its project's source directory, or the module graph is too large.

Why it happens:
A project's `src` directory is its boundary, so that a lock can account for everything
it builds. A relative import cannot climb out of it.

How to fix:
Move the file into `src`, or make it a package and add it as a dependency.

## R4001 - Integer overflow

Meaning:
An integer result does not fit in signed 64 bits.

Why it happens:
wood checks every integer operation rather than wrapping around silently.

How to fix:
Check the size of your inputs, use a float if approximate results are enough, or catch
the error with `try`.

Example that fails:
```wood
big = 9223372036854775807
print(big + 1)
```

One correction:
```wood
big = 9223372036854775807
print(float(big) + 1.0)
```

## R4002 - Division by zero

Meaning:
A division or remainder has a zero divisor.

Why it happens:
wood raises this for integers and floats, rather than returning infinity or NaN.

How to fix:
Test the divisor first.

Example that fails:
```wood
count = 0
print(10 // count)
```

One correction:
```wood
count = 0
if count != 0:
    print(10 // count)
else:
    print("nothing to divide")
```

## R4003 - Invalid value

Meaning:
A value is outside what this operation accepts.

Why it happens:
Examples are `int("abc")`, a float too large for `int`, or an exit code outside 0 to
255. The message says which.

How to fix:
Validate the value first, or catch the error with `try` when it comes from a user or file.

Example that fails:
```wood
print(int("twelve"))
```

One correction:
```wood
try:
    print(int("twelve"))
except err:
    print("not a number")
```

## R4004 - Assertion failed

Meaning:
An `assert` found its condition false.

Why it happens:
Something the program relies on is not true. The message is the one given to `assert`,
if there is one.

How to fix:
Fix the code or data that broke the assumption. Use an ordinary `if` for conditions that
can legitimately occur.

Example that fails:
```wood
assert(1 + 1 == 3, "arithmetic")
```

One correction:
```wood
assert(1 + 1 == 2, "arithmetic")
```

## R4005 - Zero step

Meaning:
A range or slice has a step of zero.

Why it happens:
Such a loop could never advance.

How to fix:
Use a nonzero step. It often comes from a computed value, so check that.

Example that fails:
```wood
step = 0
for i in range(0, 10, step):
    print(i)
```

One correction:
```wood
step = 2
for i in range(0, 10, step):
    print(i)
```

## R4006 - Index out of bounds

Meaning:
An index is not within the list, string or array.

Why it happens:
A list of `n` items has indexes from 0 to `n - 1`, and from `-n` to -1 counting from the
end. Looping one step too far is the most common cause.

How to fix:
Check the index against `len`, or loop over the items themselves.

Example that fails:
```wood
items = [1, 2, 3]
print(items[3])
```

One correction:
```wood
items = [1, 2, 3]
print(items[len(items) - 1])
```

## R4007 - Pop from an empty list

Meaning:
`pop` was called on a list with no items.

Why it happens:
A loop may remove more items than it added.

How to fix:
Check `len` first.

Example that fails:
```wood
items: list[int] = []
print(pop(items))
```

One correction:
```wood
items: list[int] = []
if len(items) > 0:
    print(pop(items))
```

## R4008 - Input or output failed

Meaning:
A file, standard stream or environment read or write failed.

Why it happens:
The file may be missing or unreadable, the disk may be full, or the program reading its
output may have closed it. The message includes the path and the reason.

How to fix:
Check the path and permissions. Catch the error with `try` when a file may not exist.

Example that fails:
```wood
print(read_text("no-such-file.txt"))
```

One correction:
```wood
try:
    print(read_text("no-such-file.txt"))
except err:
    print("no file yet")
```

## R4009 - Empty pattern

Meaning:
`split` or `replace` was given an empty pattern.

Why it happens:
An empty pattern matches everywhere, so the result would not be well defined.

How to fix:
Give a nonempty pattern. To get a string's characters, loop over the string.

Example that fails:
```wood
print(split("a,b", ""))
```

One correction:
```wood
print(split("a,b", ","))
```

## R4010 - Raised by the program

Meaning:
The program raised this error with `raise "message"`.

Why it happens:
The code or a library it calls decided it could not continue, and the message says why.

How to fix:
Read the message and fix the condition, or catch it with `try`.

Example that fails:
```wood
raise "out of cheese"
```

One correction:
```wood
try:
    raise "out of cheese"
except err:
    print(err.message)
```

## R4011 - Missing dictionary key

Meaning:
A dictionary was read with a key it does not have.

Why it happens:
The key may be misspelled, or it was never added.

How to fix:
Test with `in` first.

Example that fails:
```wood
ages = {"ana": 30}
print(ages["bo"])
```

One correction:
```wood
ages = {"ana": 30}
if "bo" in ages:
    print(ages["bo"])
else:
    print("unknown")
```

## R4012 - JSON failed

Meaning:
Text could not be parsed as JSON, or a value could not be converted to or from it.

Why it happens:
The text may be malformed or nested more than 64 levels deep, or a number may not be
finite. It also means a JSON value is not the type the program asked for.

How to fix:
Catch the error with `try` for data you did not write, and check the position in the message.

Example that fails:
```wood
print(json_parse("{broken"))
```

One correction:
```wood
try:
    print(json_parse("{broken"))
except err:
    print("not JSON")
```

## R4013 - Invalid byte

Meaning:
`byte_string` was given a number outside 0 to 255, `byte_value` a string that is not
exactly one byte, or `char_count`, `chars` or `char_slice` text that is not valid UTF-8.

Why it happens:
wood strings are bytes, so a character outside ASCII is more than one byte.

How to fix:
Check the range. Use `std.utf8` to work with characters.

Example that fails:
```wood
print(byte_string(300))
```

One correction:
```wood
print(byte_string(65))
```

## R4014 - Invalid shift count

Meaning:
A shift count is outside 0 to 63.

Why it happens:
Integers have 64 bits, so other counts have no defined result.

How to fix:
Check the count before shifting.

Example that fails:
```wood
count = 64
print(1 << count)
```

One correction:
```wood
count = 8
print(1 << count)
```

## R4015 - Clock failed

Meaning:
The system's clock could not be read, or a time value is out of range.

Why it happens:
This is rare. It usually means an argument to a time function is outside its limits, or
that the operating system refused the request.

How to fix:
Check the values given to `std.time`. If it persists, report it with the message.

## R4016 - Stream failed

Meaning:
A stream operation failed.

Why it happens:
The stream may be closed, the file may not open, or a byte count may be outside 0 to
16777216. The message includes the operating system's error.

How to fix:
Open streams with `with` so they close at the right time, and check the path and permissions.

## R4017 - Network failed

Meaning:
A network operation failed.

Why it happens:
A connection was refused, a name did not resolve, an argument is out of range, or
networking is unavailable. Networking does not exist in the browser playground.

How to fix:
Check the host, port and your connection. Catch the error with `try` to retry.

## R4018 - Network timeout

Meaning:
A socket operation did not finish before its timeout.

Why it happens:
The other side is slow or unreachable, or the timeout is too short.

How to fix:
Raise the timeout, or catch the error and retry.

## R4020 - SQLite failed

Meaning:
A database operation failed, or SQLite could not be used.

Why it happens:
SQLite 3.38 or later is loaded when first used and is not bundled, so it may be missing.
Otherwise the message is SQLite's own, such as a syntax error or a locked database.

How to fix:
Install SQLite if the message says it is unavailable. Otherwise read SQLite's message.

## R4021 - TLS failed

Meaning:
A secure connection failed, or OpenSSL could not be used.

Why it happens:
OpenSSL 3 is loaded when first used and is not bundled, so it may be missing. On Windows
it is loaded only from System32 or from an absolute `WOOD_OPENSSL_DIR`. Certificate
failures also report this code.

How to fix:
Install OpenSSL 3, or check the certificate named in the message.

## R4022 - Task failed

Meaning:
A task could not start or was cancelled, or a task operation was invalid.

Why it happens:
A task may have been waited on twice or have exceeded its limits, and an operation is
cancelled when its task is. Tasks are not available in the browser playground.

How to fix:
See the message, and docs/native-computing.md for how tasks may be used.

## R4023 - Unwrapping None

Meaning:
An optional value was unwrapped when it held `None`.

Why it happens:
A value of type `T | None` was assumed to be present.

How to fix:
Test it against `None` first.

Example that fails:
```wood
value: int | None = None
print(unwrap(value))
```

One correction:
```wood
value: int | None = None
if value != None:
    print(unwrap(value))
else:
    print("nothing")
```

## R4024 - Reading a finished step

Meaning:
An iterator's `step[T]` was read after it finished.

Why it happens:
`finished()` holds no value, so reading one fails.

How to fix:
Check `has_item(step)` first. A `for` loop does this for you.

## R4025 - Invalid callable

Meaning:
A function value is no longer valid to call.

Why it happens:
This points to a defect in generated or native code, and ordinary wood code should not
reach it.

How to fix:
Please report it, with a small program that shows it.

## R4026 - Invalid array operation

Meaning:
An array operation was given shapes or axes that do not fit.

Why it happens:
A reshape must keep the number of elements, axes must form a permutation, and sizes must
stay within the allocation limit.

How to fix:
Print the array's shape and check it. See docs/native-computing.md.

## R4027 - Math domain error

Meaning:
A math function was given a value it is not defined for.

Why it happens:
Examples are the square root of a negative number and a function that needs a finite
value being given infinity or NaN.

How to fix:
Check the value first.

Example that fails:
```wood
import std.num as num
print(num.sqrt(-1.0))
```

One correction:
```wood
import std.num as num
print(num.sqrt(1.0))
```

## R4028 - Stack overflow

Meaning:
Function calls went so deep that the stack ran out.

Why it happens:
Usually a recursive function with no base case, or one that does not get closer to it. It can
also be genuine recursion over very deep data. It cannot be caught with `try`, and the program ends.

How to fix:
Check the recursive function's stopping condition, or rewrite it as a loop.

Example that fails:
```wood
fn depth(n: int) -> int:
    return depth(n + 1) + 1
print(depth(0))
```

One correction:
```wood
fn depth(n: int) -> int:
    if n == 0:
        return 0
    return depth(n - 1) + 1
print(depth(10))
```

## R4030 - Python unavailable

Meaning:
The optional Python bridge could not start or use Python.

Why it happens:
`WOOD_PYTHON` or `VIRTUAL_ENV` may not name a usable CPython 3.14. wood itself never needs
Python, only programs that import it do. See docs/python-distribution.md.

How to fix:
Set `WOOD_PYTHON` to an absolute path to the Python you trust.

## R4031 - Python exception

Meaning:
Python code raised an exception, and the message holds its text.

Why it happens:
The error comes from the Python library being called.

How to fix:
Read the message as you would a Python error.

## R4032 - Python conversion failed

Meaning:
A value could not be converted between wood and Python.

Why it happens:
The object may be closed, of an unsupported type, or nested too deeply.

How to fix:
Convert it to plain lists, numbers and strings on the Python side.

## R4099 - Out of memory

Meaning:
A memory allocation failed.

Why it happens:
The program asked for more memory than is available, often from a list or string that
keeps growing, or an array with a very large shape. Its position may be unknown.

How to fix:
Check any loop that grows data, and process large data a piece at a time.

## D5001 - Command failed

Meaning:
The `wood` command could not do what was asked.

Why it happens:
Causes include a file that cannot be read, a wrong option, or a tool that will not start.
The message says which.

How to fix:
Follow the message. `wood --help` lists commands and options.

## D5002 - Native compilation failed

Meaning:
A tool that wood runs to build the program returned failure.

Why it happens:
LLVM tools may be missing, or linking may have failed. On Linux, wood uses LLVM 19 from
your system, so `cannot run 'opt'` means it needs installing. The tool's output is in the
message.

How to fix:
On Debian 13, run `sudo apt install llvm-19 binutils`. See docs/troubleshooting.md.

## D5003 - Process timed out

Meaning:
A process wood started ran past its time limit.

Why it happens:
A build tool or test did not finish, or a program does not stop.

How to fix:
Find the reason it does not finish, or raise the limit where the command offers one.

## D5004 - Too much output

Meaning:
A process wood started printed more than the capture limit.

Why it happens:
Usually a program in a loop, or extremely long diagnostics.

How to fix:
Reduce the output, or write it to a file.

## D5005 - Process cancelled

Meaning:
A process wood started was cancelled before it finished.

How to fix:
Run the command again.

## D5101 - Invalid package command

Meaning:
A package command's arguments or manifest are not valid.

Why it happens:
An option may not apply to this command, or `wood.json` may be missing or malformed.

How to fix:
Follow the message, and see docs/packages.md.

## D5102 - Network failed during a package command

Meaning:
A package host or the index could not be reached, or it rate limited the request.

How to fix:
Check your connection and try again. `--offline` uses packages already in the cache.

## D5103 - Package version not available

Meaning:
A reference, commit or version cannot be found, or it has been revoked.

Why it happens:
The reference may have been deleted upstream, or the version withdrawn for a security reason.

How to fix:
Pick another version. Follow the message if it names a revocation reason.

## D5104 - Invalid package metadata

Meaning:
A package index, catalog entry or manifest has a form wood does not accept.

How to fix:
If it is your package, correct the metadata. See docs/package-publishing.md.

## D5105 - Package cache or fetch failed

Meaning:
A package could not be fetched into or read from the cache.

Why it happens:
Git may be missing, the disk may be full, or a cached entry may be damaged.

How to fix:
Check that `git` runs, then free some disk space. See docs/packages.md.

## D5106 - Package graph conflict

Meaning:
The dependencies cannot be resolved together.

Why it happens:
Two packages require different versions of a third, the graph has a cycle, or it holds
more than 128 packages. wood does not pick a version for you.

How to fix:
Make the requirements agree, using the versions the message names.

## D5107 - Unsafe or altered package

Meaning:
A package failed a safety or integrity check.

Why it happens:
It may hold an unsafe path or URL, or a filesystem link, or differ from the hash recorded
for it. wood refuses rather than guessing.

How to fix:
Do not bypass this. Confirm the package's source, and report it to its owner if the hash changed.

## D5108 - Package not available offline

Meaning:
`--offline` was used, and what was asked for is not in the cache.

How to fix:
Run once online to fill the cache, or name an explicit version already in the lock.

## D5109 - Package not in the catalog

Meaning:
The catalog does not list this package.

How to fix:
Check the name. A package does not need to be listed: `wood add github@owner/repository` installs it directly.
