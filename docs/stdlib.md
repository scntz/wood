# Standard library

This guide covers the built-in functions and how the standard modules behave. The [API index](stdlib-api.md) lists every
public function, record, field and method, generated from the source, and the [module overview](stdlib-modules.md) gives
each module's promises and errors in one table. Don't assume a Python or C++ library function exists in wood: check here.

A few modules have their own pages:

- `std.num` (arrays, views, reductions, matrix products), `std.linalg` (vectors, transforms, linear solves) and
  `std.geometry` (triangle meshes and OBJ files) are in [native computing](native-computing.md) and [OBJ files](obj.md).
- `std.io`, `std.net` and `std.http` (files, TCP and HTTP) are in [networking](networking.md).
- `std.sqlite`, `std.tls`, `std.https` and `std.task` are in [backend services](backend.md).

Built-in functions need no import. A function or import of your own with the same name always takes priority, and most
built-in names aren't reserved, so you can still use them as variable names. Arguments are evaluated left to right. A
runtime error can be caught with `try` / `except name`; an uncaught one ends the program with exit code 70. See
[errors](errors.md).

## Collections and output

### Reusable native algorithms (`std.functional`)

Eager, type-safe algorithms over lists:

| Function | Result |
| --- | --- |
| `map[T,U](transform: fn(T)->U, items: list[T])` | `list[U]` |
| `filter[T](predicate: fn(T)->bool, items: list[T])` | The matching items, in their original order |
| `fold[T,U](step: fn(U,T)->U, items: list[T], initial: U)` | The accumulated value, or `initial` for an empty list |
| `first[T](items: list[T])` | `T \| None`; `None` when empty |
| `find_first[T](predicate: fn(T)->bool, items: list[T])` | The first match or `None`; stops at the first match |
| `zip[T,U](left: list[T], right: list[U])` | `list[tuple[T,U]]`, as long as the shorter list |
| `enumerate[T](items: list[T], start: int = 0)` | `list[tuple[int,T]]`, with checked indexes |
| `partition[T](predicate: fn(T)->bool, items: list[T])` | `tuple[list[T],list[T]]` of matches and the rest |
| `lookup[K,V](items: dict[K,V], key: K)` | `V \| None` |

Callbacks can be functions or closures. Each predicate runs once per item, and `partition` keeps the order in both lists.
The results are new lists, not lazy iterators. See [types and calls](types-and-calls.md).

### Lazy native algorithms (`std.iter`)

`map`, `filter`, `take`, `drop`, `enumerate`, `zip`, `chain`, `from_fn` and `lines` build lazy pipelines, taking the
source first. `collect`, `fold`, `count`, `any`, `all` and `find_first` consume them. The [iterator guide](closures-iterators.md)
covers custom iterators and ranges.

`iter`, `next`, `yielded`, `finished`, `has_item` and `step_value` are available without an import. Taking the value of an
empty step raises R4024.

### Builtins

| Function | Behavior |
| --- | --- |
| `print(values...) -> void` | Writes the values separated by spaces, then a newline; `print()` writes a blank line |
| `eprint(values...) -> void` | The same, on stderr |
| `exit(code: int) -> void` | Ends the program with a status from 0 to 255, skipping handlers; other codes raise R4003 |
| `str(value) -> str` | Formats any value; lists print as `[element, ...]`, with string elements quoted |
| `len(str or list[T] or dict[K,V] or tuple[...] or array[T]) -> int` | Bytes for strings, elements for everything else |
| `unwrap(value: T \| None) -> T` | The value, or R4023 if it's `None` |
| `append(target: list[T], value: T) -> void` | Appends to a variable or path; an `int` can go into a `list[float]` |
| `pop(target: list[T]) -> T` | Removes and returns the last element; R4007 if the list is empty |
| `slice(value: str or list[T], start: int, stop: int)` | A copy between two offsets, clamped to the length. Negative offsets count from the end, and `stop` is excluded |
| `keys(mapping: dict[K,V]) -> list[K]`, `values(mapping) -> list[V]` | New lists, in insertion order |
| `get(mapping: dict[K,V], key: K, fallback: V) -> V` | The value, or `fallback`, which is always evaluated |
| `remove(target: dict[K,V], key: K) -> bool` | Removes a key from a variable or path; `false` if it wasn't there |
| `assert(condition: bool [, message: str]) -> void` | Checks at every optimization level; failure raises R4004. The message is always evaluated |

Copying a list is cheap: the copies share storage until one of them changes. See [collections](language.md#collections)
for nested changes, evaluation order and iteration. `str(list)` is for display: it doesn't promise to reproduce the source
literal for arbitrary bytes.

## Numbers

`int(value)` accepts an `int`, `float` or `str`. A float is truncated toward zero, and infinite, NaN or out-of-range values
are rejected. A string must be a decimal integer with at most one `+` or `-` and no spaces or suffix. Leading zeros are
still decimal.

`float(value)` accepts an `int`, `float` or `str`. A string must be a finite decimal number, with an optional sign,
fraction and exponent, and digits on both sides of a decimal point. `nan`, `inf`, hex, spaces, suffixes, overflow and
nonzero values that round to zero are rejected. Floats from arithmetic can still be infinite or NaN.

Call `strip(text)` first if the input may have spaces around it. A failed conversion raises R4003. Parsing and formatting
ignore the system locale.

## Text

Strings are bytes. All offsets are byte offsets, and nothing normalizes Unicode, folds case or indexes by character; use
[`std.utf8`](#bytes-and-utf-8) for characters.

| Function | Behavior |
| --- | --- |
| `split(text: str, separator: str) -> list[str]` | Splits on each separator, keeping empty fields at the start, end and between repeats; `split("", ",")` is `[""]` |
| `join(items: list[str], separator: str) -> str` | Joins with the separator; an empty list gives `""` |
| `strip(text: str) -> str` | Removes ASCII spaces, tabs, line feeds, carriage returns, form feeds and vertical tabs from both ends |
| `find(text: str, needle: str) -> int` | The first byte offset, or `-1`; an empty needle matches at 0 |
| `replace(text: str, old: str, replacement: str) -> str` | Replaces every occurrence, without searching the replacement again |
| `starts_with(text: str, prefix: str) -> bool` | Exact prefix test; an empty prefix always matches |
| `ends_with(text: str, suffix: str) -> bool` | Exact suffix test; an empty suffix always matches |

An empty separator for `split`, or an empty `old` for `replace`, raises R4009. NUL and any other byte are kept as they are.

## Files and paths

| Function | Behavior |
| --- | --- |
| `read_text(path: str) -> str` | Reads the whole file as bytes, with no newline or encoding conversion |
| `write_text(path: str, content: str) -> void` | Creates or truncates the file and writes the bytes, checking for write and close errors |
| `path_exists(path: str) -> bool` | Whether the path exists, following symbolic links |

Paths are UTF-8 on Windows and bytes on Linux, and relative paths start from the working directory. A missing file, denied
access, a bad path (including one with NUL) or any other I/O error raises R4008, except that `path_exists` returns `false`
for a missing path. Parent directories are never created for you. Despite its name, `read_text` doesn't check that the file
is UTF-8.

Reading a whole file needs as much memory as the file. Writing truncates first, so a failure partway through can leave a
partial file; there's no atomic save. For large files, use the streams in [std.io](networking.md#stdio).

## Arguments and standard input

`args() -> list[str]` returns the program's arguments, without the program name. With `wood run`, pass them after `--`:
`wood program.wd -- input.txt "two words"`. A built executable gets them directly. Empty arguments and arguments starting
with `-` are kept, and Windows arguments are converted to UTF-8. Each call returns a fresh list.

## Environment configuration

`import std.env as env` gives you `get(name: str) -> str | None`. A missing variable returns `None`, and a variable set to
an empty value returns `""`. The result is an ordinary string you can keep, put in a closure or return from a task. There's
no way to set or list variables; for program options, use `args()`.

A name must not be empty or contain NUL or `=` (R4003). Windows looks names up case-insensitively and converts values to
UTF-8; Linux uses exact bytes and is case-sensitive. A value that can't be read or converted raises R4008, and is never
silently truncated.

Treat the environment as fixed when the program starts. Changing it from foreign code, such as Python's `os.environ`, isn't
supported, and doing so from another thread is unsafe. Don't print secrets from the environment in error messages.

The [configured report](../examples/configured_report.wd) example reads `WOOD_CSV_DELIMITER`, defaulting to a comma, then
reads a CSV file, totals it and writes JSON to a new file.

## Secure randomness

`import std.random as random` gives you:

| Function | Contract |
| --- | --- |
| `bytes(size: int) -> str` | `size` random bytes, from 0 to 1,048,576. Other sizes raise R4003 |
| `below(bound: int) -> int` | A uniform integer with `0 <= value < bound`, for any positive `bound`. A bound of 0 or less raises R4010 |

The randomness comes from the operating system: `BCryptGenRandom` from System32 on Windows, `getrandom` on Linux. If the OS
can't provide it, the call raises R4008 rather than returning partial or weaker output. There's no seed and no reproducible
generator: every run differs.

`below` rejects values that would bias the result, retrying up to 256 times before raising R4010. Each attempt succeeds at
least half the time, so this only happens if the OS source is broken. Random strings are ordinary values, not protected
secret storage. For cryptographic protocols, use a dedicated library: this module only supplies the randomness.

## Standard input

`eof() -> bool` checks whether stdin has more data without consuming it, and may wait for interactive input.
`read_line() -> str` reads up to a line feed and removes it, along with a carriage return before it. A last line with no
line feed is returned as is, and at the end of input it returns `""`. Read errors raise R4008. This loop handles empty lines
and a last line without a line feed:

```wood
while not eof():
    line = read_line()
    print(line)
```

[text_report.wd](../examples/text_report.wd) reads a file and writes a report, and [filter_stdin.wd](../examples/filter_stdin.wd)
filters stdin.

## Embedded modules

These modules are wood source built into the compiler, so they need no search path. Import single functions
(`from std.math import gcd`) or a whole module (`import std.collections as collections`). Their source is in
[stdlib/](../stdlib) here, and installed alongside these docs.

| Module and signature | Contract |
| --- | --- |
| `std.math.gcd(a: int, b: int) -> int` | Greatest common divisor, never negative; `gcd(0,0)` is 0. The minimum integer overflows on negation |
| `std.math.lcm(a: int, b: int) -> int` | Least common multiple, never negative; 0 if either is 0. Can overflow |
| `std.math.pow_int(base: int, exponent: int) -> int` | Integer power by squaring; the exponent must not be negative. `0^0` is 1. Overflow is an error |
| `std.math.floor`, `ceil`, `trunc`, `round` `(value: float) -> float` | Rounding to whole numbers; `round` takes halves away from zero |
| `std.math.exp`, `log`, `log2`, `log10`, `tan`, `atan`, `asin`, `acos` `(value: float) -> float` | Within three units in the last place of reference values. `log(0.0)` is negative infinity, and arguments outside the domain give NaN |
| `std.math.pow(base: float, power: float)`, `atan2(y: float, x: float)`, `hypot(a: float, b: float)` | The same accuracy; whole powers are computed by squaring |
| `std.math.pi()`, `e()`, `radians(value: float)`, `degrees(value: float)`, `abs(value: float)`, `sqrt(value: float)`, `sin(value: float)`, `cos(value: float)` | Constants and conversions |
| `std.math.clamp_int(value: int, low: int, high: int) -> int` | Clamps to an inclusive range; `low` above `high` raises R4010 |
| `std.stats.sum_int(values: list[int]) -> int` | Checked sum, left to right; an empty list sums to 0 |
| `std.stats.mean(values: list[float]) -> float` | Mean, computed in one pass; an empty list raises R4010 |
| `std.stats.variance(values: list[float]) -> float` | Population variance, using a scaled Welford update; an empty list raises R4010 |
| `std.collections.sorted_int(values: list[int]) -> list[int]` | Stable merge sort: O(n log n) comparisons and O(n) extra space |
| `std.collections.binary_search(values: list[int], needle: int) -> int` | The first equal index, or -1, in a sorted list; O(log n) comparisons |

`mean` and `variance` use ordinary 64-bit floats, so they round, and can overflow or produce infinities. They avoid
overflow in intermediate steps where they can, and have tests for extreme values, but they don't promise correct rounding.
Sorting is only for integers: for anything else, use `std.collections.Heap[T]` with a comparator, described with `Stack`
and `Queue` in [generic records](generic-records.md).

## JSON

`json(value) -> json`, `json_parse(text: str) -> json`, `json_dump(value) -> str` and `json_null() -> json` convert between
typed values and immutable JSON. `json_kind` tells you what a JSON value holds, and `json_int`, `json_float`, `json_bool`,
`json_str`, `json_list` and `json_dict` extract it, raising R4012 on a mismatch. See [typed JSON](data-model.md#typed-json-boundaries)
for which types convert, and for limits, equality and key order.

## CSV

`import std.csv as csv` gives you:

| Signature | Contract |
| --- | --- |
| `parse(text: str, delimiter: str) -> list[list[str]]` | Quoted and unquoted fields, doubled quotes, line breaks inside quotes, LF, CR and CRLF line endings, and a last line with no line ending |
| `dump(rows: list[list[str]], delimiter: str) -> str` | Quotes only where needed, doubles quotes, and ends every row with CRLF; no rows gives `""` |
| `read_table(text: str, delimiter: str) -> list[dict[str,str]]` | Uses the first row as the header; duplicate headers and rows of the wrong width are errors |

The delimiter is exactly one byte, and can't be a quote, CR, LF or NUL. A blank line becomes an empty list, and a quoted
empty field becomes `[""]`. `parse("")` returns no rows. Spaces inside fields are kept. A quote inside an unquoted field,
text after a closing quote, or an unclosed quote raises R4010. `read_table` needs a header row; `parse` and `dump` allow
rows of different widths.

The format follows the quoting rules of [RFC 4180](https://www.rfc-editor.org/info/rfc4180/), and is tested against
Python's [csv module](https://docs.python.org/3/library/csv.html). It works on whole documents in memory: there's no
streaming reader, dialect detection, encoding conversion or escaping of spreadsheet formulas.

## Bytes and UTF-8

`byte_value(text: str) -> int` takes exactly one byte and returns 0 to 255. `byte_string(value: int) -> str` does the
reverse. Anything else raises R4013. They work on bytes, not characters.

Three builtins, with no import, work on characters, meaning Unicode scalar values in strict UTF-8:

| Builtin | Contract |
| --- | --- |
| `char_count(text: str) -> int` | The number of characters |
| `chars(text: str) -> list[str]` | Each character as its own string, so `for c in chars(text):` loops over characters |
| `char_slice(text: str, start: int, stop: int) -> str` | Characters `start` to `stop`, excluded; negative offsets count from the end and both are clamped, as in `slice` |

Malformed UTF-8 raises R4013 with `text is not valid UTF-8`. They check the same rules as `std.utf8` below, are written in
the runtime rather than in wood, and keep existing functions or variables called `chars` working.

`import std.utf8 as utf8` gives you:

| Function | Contract |
| --- | --- |
| `decode(text: str) -> list[int]` | Strict UTF-8 to Unicode scalar values |
| `encode(points: list[int]) -> str` | Scalar values to UTF-8; rejects negatives, surrogates and values above U+10FFFF |
| `is_valid(text: str) -> bool` | `false` for malformed UTF-8 |
| `length(text: str) -> int` | The number of scalar values, checking the whole string |
| `characters(text: str) -> list[str]` | One string per scalar value |
| `slice_text(text: str, start: int, stop: int) -> str` | Slices by scalar value, with negative offsets and an excluded `stop` |

It rejects overlong encodings, encoded surrogates, stray or truncated continuation bytes and values above U+10FFFF, as
[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629.html) requires, raising R4010 with the byte offset. NUL, noncharacters
and a byte order mark are kept. A scalar value isn't a grapheme, and there's no normalization or case folding.
`str`, `len` and indexing still work on bytes. See [unicode_report.wd](../examples/unicode_report.wd).

## Binary integer formats

`import std.binary as binary` gives you `pack_int(value: int, width: int, order: str)` and `pack_uint(...)`, which return
`str`, and `unpack_int(text: str, order: str)` and `unpack_uint(...)`, which return `int`. The width is 1 to 8 bytes, and
the order is `little` or `big`. Signed values use two's complement. A bad width or order, or a value that doesn't fit,
raises R4010. When unpacking, the width is the length of the input. Unsigned 8-byte values above the largest `int` are
rejected.

There are no float formats, arbitrary widths or streaming buffers. The [archive project](../examples/archive/wood.json)
shows versioned headers, mixed byte orders and length checks.

## Native filesystem and paths

`import std.fs as fs` gives you:

| Function | Contract |
| --- | --- |
| `kind(path: str) -> str` | `missing`, `file`, `directory`, `symlink` or `other`, without following a final link |
| `entries(path: str, limit: int) -> list[str]` | The names in a directory, sorted by bytes; fails if there are more than `limit` |
| `size(path: str) -> int` | A regular file's size in bytes, following links |
| `make_dirs(path: str) -> bool` | Creates the directory and any missing parents; `true` if it created any |
| `remove(path: str) -> bool` | Removes one file, link or empty directory; `false` if it wasn't there. Never recursive |
| `walk_files(root: str, max_entries: int) -> list[str]` | Every regular file under `root`, depth first, skipping links; fails after `max_entries` entries |
| `find_files(root: str, predicate: fn(str) -> bool, max_entries: int) -> list[str]` | The files from `walk_files` for which `predicate` returns `true` |

The entry limits count directories and skipped links, but not the root. A root that isn't a directory or is a link, a
negative limit, or an entry that disappears during the walk raises R4010. OS errors and going over a listing limit raise
R4008. A failed walk returns nothing, though callbacks that already ran aren't undone.

These functions return whole lists, not a consistent snapshot: another program can change a directory while you walk it,
so skipping links isn't a security boundary. Use an OS sandbox to contain untrusted code. There's no recursive removal,
permission or timestamp access, or file watching.

`import std.path as path` gives you `combine(base: str, child: str)`, `name(path: str)`, `parent(path: str)`,
`absolute(path: str)` and `normalize(path: str)`, which all return `str`. `combine` follows the host's rules, so an absolute
child replaces the base, and it doesn't keep the child inside the base. `normalize` works on the text only, without
following links or checking the path exists. `absolute` prefixes the working directory. A bare filename has an empty
parent, and a path ending in a separator has an empty name. NUL is rejected everywhere.

These resolve links, using the same code as the compiler:

| Function | Contract |
| --- | --- |
| `resolve(path: str, allow_missing: bool = false) -> str` | The absolute canonical path, with `/` separators. The path must exist, unless `allow_missing=true`, which resolves the part that exists and normalizes the rest |
| `identity(path: str) -> str` | A canonical identity for an existing path, lowercasing ASCII letters on Windows. For comparing paths, not for display |
| `within(path: str, root: str) -> bool` | Whether `path` is `root` or inside it, after resolving links. `root` must exist; `path` needn't |
| `is_absolute(path: str) -> bool` | Whether the path is absolute, without touching the filesystem |

Errors raise R4008. A check doesn't stop another process replacing the path before you open it. On Windows, only ASCII
letters compare case-insensitively.

`fs_kind`, `fs_list`, `fs_size`, `fs_mkdirs`, `fs_remove`, `path_join`, `path_name`, `path_parent`, `path_absolute`,
`path_normalize`, `path_resolve`, `path_identity`, `path_within` and `path_is_absolute` are the same functions without an
import (`path_resolve` needs its second argument). The [tree report](../examples/tree_report.wd) example combines a callback,
a bounded walk, records and JSON output.

## Native time

`import std.time as time` gives you:

| Function | Contract |
| --- | --- |
| `sleep(seconds: float) -> void` | Blocks the current thread for 0 to 86,400 seconds; an `int` works too |
| `monotonic() -> float` | Seconds from a steady clock that never goes backwards |
| `monotonic_ns() -> int` | The same clock in integer nanoseconds |
| `perf_counter() -> float` | The same clock, for timing |
| `perf_counter_ns() -> int` | The same in integer nanoseconds |

The clocks start counting the first time one is read, so their values only mean something as differences within one run.
They aren't Unix time and include time spent sleeping. Resolution depends on the OS and hardware: nanosecond units don't
mean nanosecond accuracy. For precise timing, subtract two `_ns` readings before converting to seconds.

`sleep(0)` returns immediately, and a positive fraction of a nanosecond rounds up. A negative, infinite or NaN duration, or
more than a day, raises R4015, as do clock failures. The OS may sleep longer than you asked. There are no dates, wall-clock
time, time zones or CPU time.

```wood
import std.time as time
started = time.perf_counter_ns()
time.sleep(0.02)
elapsed_ns = time.perf_counter_ns() - started
print("elapsed seconds:", float(elapsed_ns) / 1000000000.0)
```

[timing.wd](../examples/timing.wd) times a workload and checks its result. One reading isn't a benchmark: see
[performance](performance.md#measuring-well).
