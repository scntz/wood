# Application data

wood's container and record values preserve value isolation through assignment,
function calls, indexed reads, and nested mutation. This contract also applies
when containers are stored inside user-defined data. The compiler evaluates
mutation indexes and other arguments before resolving mutable paths, so an
argument can resize a container without leaving a dangling native reference.
Mutable methods additionally validate their receiver path before arguments, then
reacquire it afterward; see the method receiver contract for precise failure order.
`stream` fields/elements are an explicit identity-bearing exception: copying a
handle shares its OS resource/cursor; closing any copy closes every alias. The
container/record still isolates assignments to its fields/elements. See
[resource lifetime](language.md#scoped-native-resources); streams cannot enter JSON.

## Dictionaries

`dict[K, V]` maps homogeneous keys to homogeneous values. Keys are exactly `int`,
`str`, or `bool`; bool and int are distinct types. Literals use `{key: value}`;
an empty `{}` needs context such as `counts: dict[str, int] = {}`. Values may
contain other containers. Literal entries evaluate key then value, left to right.
Repeated keys replace their values and keep the first insertion position.

`mapping[key]` reads a value or raises R4011 if absent. Assignment inserts or
replaces the final key; intermediate keys in a nested mutation must exist.
`get(mapping, key, fallback)` returns a value or its explicitly evaluated fallback.
`remove(mapping, key)` mutates a rooted variable/path and returns whether a key
was removed. Removing and reinserting a key appends it at the end.

`keys(mapping)` and `values(mapping)` return independent lists in insertion order.
`for key in mapping` visits a snapshot of the original keys; mutating the source
does not affect iteration. Dictionary equality compares keys and values regardless
of insertion order. Ordering dictionaries is rejected. Membership `in` / `not in`
tests dictionary keys, list elements, or byte substrings for strings.

Storage is copy-on-write. Lookup, membership, and unique-storage insertion/update
use a balanced tree index and take O(log n) key comparisons. Deletion is O(n),
including index maintenance and shifting insertion-order entries. First shared
mutation copies the affected dictionary; snapshots/keys/values require O(n) work.
These are algorithmic bounds, not measured execution claims. The tree index
avoids relying on predictable hash functions for untrusted string keys. It does
not claim Python's average constant-time hash-table performance.

Python's [mapping contract](https://docs.python.org/3/library/stdtypes.html#mapping-types-dict)
informs insertion order and order-independent equality. wood retains its own
static types and value semantics: mutation of a function parameter does not change
the caller's dictionary. Floats, containers, and user data are not dictionary keys.

## Records

`record Point:` followed by indented `x: float` and `y: float` declares a nominal
value type and its constructor `Point(x, y)`. Every field requires a type and
every call supplies all fields. Positional fields follow declaration order;
keywords can name fields in any order. Supplied expressions evaluate in written
order. Int arguments can widen into float fields. An empty record uses `pass` and `Empty()`.
Fields are read and assigned through `value.field`, including rooted nested
container paths. An expression temporary cannot be mutated.

Records copy by value. Their container fields share copy-on-write storage; nested
mutation preserves earlier copies. Equality recursively compares all fields;
different nominal record types cannot be compared or implicitly converted.
Constructors accept positional/keyword fields. Generic records and native read/mutable
methods are supported; see [generic-records.md](generic-records.md). Ordering, inheritance,
field defaults, and runtime reflection are not implemented. Tuples and optional fields
follow [native typed abstractions](types-and-calls.md).

Records can be declared after their use, passed/returned by functions, and stored
in containers. Direct by-value containment must be acyclic. Recursive data uses a
container, such as `children: list[Tree]`. Import public records exactly like
functions, with aliases or qualified module names in both annotations and
constructors. Underscore records are private and imports do not re-export types.
Inspection includes record fields and their resolved types.

## Typed JSON boundaries

List concatenation (`+`), integer repetition (`*`), slice notation with steps and
list/dictionary comprehensions preserve these value semantics. A repeated nested
list detaches on mutation instead of changing its siblings. Dictionary
comprehensions evaluate keys before values and retain insertion positions when
replacing duplicate keys. Slice assignment is not supported.

`json` is an immutable, heterogeneous JSON value, not an implicit dynamic escape
from static types. `json_parse(text)` parses strict JSON; `json(value)` converts
a typed value; `json_dump(value)` converts and serializes it. Scalars, lists,
string-key dictionaries, records and error values can be converted recursively.
Non-string dictionary keys are rejected at compile time. `json_null()` supplies
JSON null. Native optional values use `T | None` with explicit `unwrap`;
they are distinct from JSON trees and are not nullable raw references.

`json_kind(value)` returns `null`, `bool`, `int`, `float`, `str`, `list`, or `dict`.
Extract values explicitly with `json_bool`, `json_int`, `json_float`, `json_str`,
`json_list` (list[json]) and `json_dict` (dict[str,json]). `json_float` accepts both
JSON number representations and widens integers using binary64 rules. Other
extractors require the exact kind. JSON equality is structural and kind-sensitive;
`json(1)` differs from `json(1.0)`. Float serialization retains a fractional marker.

Values share immutable nodes, so copying a JSON value is O(1). Extracting a list
or dictionary creates a container of shared child values. Mutating that container
does not change the original JSON value. String extraction copies bytes.

The compiler's manifest/report parser and native JSON APIs share one
implementation. It follows [RFC 8259](https://www.rfc-editor.org/info/rfc8259/),
with duplicate-key rejection, signed 64-bit integer and finite binary64 domains,
strict UTF-8, paired Unicode surrogate validation, and at most 64 nesting levels.
Native input is limited to 16 MiB. Parse/type/conversion errors raise R4012 at the
wood call site. Typed conversion rejects nonfinite numbers and invalid UTF-8;
diagnostic serialization continues replacing invalid bytes to keep reports valid.
Objects serialize with lexicographically ordered keys. Their textual insertion
order is not preserved by parsing/extraction.

Whole-document parsing/serialization uses memory proportional to the document.
The APIs provide no streaming parser, arbitrary-precision JSON numbers, automatic
record deserializer, schema language, or process-memory quota. Domain functions
explicitly validate external data and construct typed records. See the
[catalog example](../examples/catalog/wood.json) and its imported
[product schema](../examples/packages/catalog_schema/src/models.wd).
