# Recoverable errors and process control

wood supports a single structured error value and an explicit handler:

```wood
fn read_count(path: str) -> int:
    try:
        return int(strip(read_text(path)))
    except err:
        eprint(err.code + ": " + err.message)
        return 0
```

`except name:` binds an `error` value in the handler's lexical scope. It has
read-only fields `code`, `message`, `file` (str), and `line`, `column` (int).
It can be passed to functions, copied, stored in a list, formatted, compared for
equality, or raised again. Fields cannot be assigned. A handler binding shadows
an outer name only within that handler.

`raise "message"` reports R4010 at that statement. `raise err` preserves the
original code, message, and origin, and a bare `raise` inside an `except` block does
the same for the error being handled, even where a loop or nested handler reuses the
name. Outside an `except` block a bare `raise` is E3017. A raise ends the current
control-flow path; nonvoid functions can unconditionally raise.

`error(code, message)` makes an error with the program's own code, so callers can tell
failures apart without parsing messages. Its `file`, `line` and `column` are where
`error` was called. A code is 1 to 64 ASCII letters, digits, `.`, `_` or `-`, starts
with a letter, and is not shaped like wood's own codes (a capital letter and four
digits); anything else raises R4003. Codes like `app.not_found` read well.

```wood
fn load(name: str) -> str:
    if name == "":
        raise error("app.no_name", "a name is required")
    return name
try:
    load("")
except err:
    if err.code != "app.no_name":
        raise
    print("asked for a name")
```

```wood
fn first_line(path: str) -> str:
    print("opening", path)
    try:
        return split(read_text(path), "\n")[0]
    except err:
        return ""
    finally:
        print("done with", path)
print(first_line("missing.txt"))
```

A `finally:` block follows the handler, or comes straight after the try body when
there is no handler. It runs however the statement is left: after normal
completion, on `return`, `break` and `continue`, and when an error leaves the try
body or the handler, which it then raises again. If the finally block raises, its
error replaces the one that was leaving. It cannot `return`, `break` or `continue`
out of the statement (E3011, E3012); loops inside it may. `exit` ends the program
without running finally blocks, like it skips handlers. `with` blocks inside the try
body close before the finally block runs. `finally` is a keyword only right after a
try block, so a variable can still be called `finally`.

Handlers catch wood checked runtime errors, including assertions, integer errors,
index/pop errors, numeric text parsing, file/path/stdin errors, and explicit raises.
They catch failures in called/imported functions too. They do not catch arbitrary
C++ allocation/library failures, OS signals, stack overflow, or AddressSanitizer
failures. Those remain outside recoverable language errors. A stack overflow on the main thread
ends the program with exit 70 and R4028, in words even when built with `--diagnostics=json`.

A try block and its handler have separate lexical scopes. Initialize a variable
outside the try if it must be used afterward. Earlier mutations and I/O are not
rolled back when an error is caught. Typed exception hierarchies, multiple handlers,
traceback objects, and user-defined error classes are future work.
`with` provides deterministic cleanup for native resources. It reports normal
close errors, but suppresses secondary close failures while preserving an error
or exit already unwinding. Explicitly flush important output before other fallible
work. Transaction resources explicitly roll back unless committed; task cleanup
requests cancellation and joins. Cooperative socket cancellation reports R4022;
synchronous DNS and arbitrary OS/foreign calls are not forcibly interrupted.
R4020 identifies SQLite errors, R4021 TLS errors,
and R4022 task-operation errors; task waits preserve original worker error codes.
The optional LLVM [Python bridge](python-distribution.md) uses R4030 for
interpreter/session failures, R4031 for Python exceptions, and R4032 for conversion
or lifetime errors. Python exceptions are catchable; unsafe native-extension
faults are not. Programs that don't use Python never need it.
Borrowed C callbacks defer errors until the foreign call returns. See
[streams](networking.md) and [native backend contracts](backend.md).

`eprint(values...)` formats like `print`, writes to stderr, and ends with a newline.
`exit(code)` requires an int in 0..255 and unwinds to the native entrypoint. It is
not caught by `except`; local C++ value destructors run during unwinding. Invalid
exit codes raise R4003 and can be handled. The process otherwise exits 0 after
top-level statements. An unhandled wood error exits 70. A test fails on any nonzero
exit, including an explicit application status.

```wood
if len(args()) != 1:
    eprint("usage: app input.txt")
    exit(2)
try:
    print(read_text(args()[0]))
except err:
    eprint(err.code, err.message)
    exit(1)
```

Human and JSON unhandled diagnostics preserve the source module and origin.
`str(err)` produces `code + ": " + message`. JSON reporting remains selectable with
`--diagnostics=json`; captured errors are ordinary values and produce no automatic
output. A C++ library failure reported as R4099 has an unknown (zero) source position.

Closure capture/callable-shape violations use E3028; iterator protocol and associated
type violations use E3029. Reading a finished `step[T]` raises R4024. The native
owned-callable runtime rejects invocation after a C++ move with R4025 (wood source
value assignments copy, so ordinary source programs do not produce that state).
