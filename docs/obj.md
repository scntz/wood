# Native OBJ mesh importing

`std.geometry.read_obj` loads a deliberately small, geometry-only Wavefront OBJ
subset into the existing `Mesh` record. The parser is written entirely in wood
using scoped streams, numeric conversion, lists and contiguous arrays. It does
not require Python, external libraries or an additional compiler backend.

```wood
import std.geometry as geo
import std.linalg as la
import std.num as num

mesh = geo.read_obj("input.obj")
print("vertices:",num.shape(mesh.vertices)[0])
print("triangles:",num.shape(mesh.triangles)[0])
print("bounds:",geo.bounds(mesh))
result = geo.transformed(mesh,la.translation(1.0,2.0,-3.0))
geo.write_obj(result,"output.obj")
```

The complete [mesh_import example](../examples/mesh_import.wd) additionally applies
uniform scaling and prints input/output areas and transformed bounds:

```powershell
wood examples/mesh_import.wd -- input.obj output.obj
```

## Accepted format

Records are physical LF-delimited lines, with CRLF and a final line without LF
accepted. ASCII spaces and tabs separate fields. Blank lines, leading/trailing
whitespace and `#` comments, including trailing comments, are accepted. Comments
can contain UTF-8. NUL bytes are rejected, including inside comments. A UTF-8 BOM
and backslash line continuations are unsupported.

| Record | Accepted form |
| --- | --- |
| Vertex | `v x y z`, exactly three finite binary64 coordinates |
| Normal | `vn x y z`, exactly three finite components; validated but not retained |
| Triangle without normals | `f v1 v2 v3` |
| Triangle with normals | `f v1//n1 v2//n2 v3//n3` |

Coordinates accept signed decimal integers/fractions and decimal exponents,
including `.5`, `1.`, `-.5` and `1.e2`. NaN, infinity, hexadecimal, malformed
numbers, overflow and nonzero values rounding to zero are rejected. Normal
components need not be unit length; they are only checked for finite conversion.

Indices are signed decimal integers. Positive indices are one-based; negative
indices count backward from the records available **at that face**, with `-1`
meaning the most recent. Zero and forward/out-of-range references are errors.
Vertex and normal indices address independent record sequences. A face must use
one corner spelling throughout, although different faces may choose differently.
Original vertex order, triangle order, winding and coordinates are preserved.

All other directives are rejected, including texture coordinates, materials,
groups, object names, smoothing groups, lines, points, polygons with more than
three corners, weighted/colored vertices, curves, surfaces, includes and shell
commands. The importer never opens an included/material file or executes a
command. Unsupported data is not silently ignored or triangulated.

This is a subset of the original [Wavefront Appendix B1 specification](https://raw.githubusercontent.com/mokiat/java-data-front/master/documents/OBJ%20Specification.txt),
not all of OBJ. Separate vertex and normal indexes and relative indexes work as that format describes; the records and
limits above are what wood accepts.

## Geometry and normal semantics

The returned `Mesh` has contiguous `(vertices,3)` float and `(triangles,3)` integer
arrays, with wood's existing snapshot/value semantics and zero-based triangle
indices. Empty files/comment-only files return shapes `(0,3)` for both buffers.

`Mesh` has no stored shading-normal field. Input normals and normal references
are validated and then discarded. `write_obj` recomputes area-weighted vertex
normals from the final geometry. Round-trips preserve geometry, not authored
normal directions, hard shading seams, comments or original text formatting.

Import accepts the same geometric validity contract as `geo.validate`: finite
coordinates and in-range triangle indices, including empty meshes, unused
vertices and degenerate triangles. It does not prove manifoldness, orientation
consistency or nondegeneracy. `bounds` rejects an empty mesh. `write_obj` can reject
vertices with no normalizable accumulated normal. Such a failure happens before
output is opened. I/O failures after opening output can still leave partial data.

## Limits and errors

`read_obj(path, *, max_bytes=16777216, max_vertices=1000000,
max_triangles=1000000, max_normals=1000000, max_line_bytes=4096)` accepts keyword
budgets. Limits are checked before opening the input.

| Budget | Default | Accepted range |
| --- | ---: | ---: |
| Total input bytes | 16,777,216 | 0..1,073,741,824 |
| Vertices | 1,000,000 | 0..1,000,000 |
| Triangles | 1,000,000 | 0..1,000,000 |
| Normal records | 1,000,000 | 0..1,000,000 |
| Bytes in one physical line | 4,096 | 1..65,536 |

Bytes include comments, whitespace and line terminators. Exact limits succeed;
an excess-detection read can consume one additional byte. A zero total budget
therefore accepts only an empty file. Zero record budgets prohibit those records.
There is no partial-success result or silent truncation when a budget is exceeded.

Parsing streams one line at a time. At most four field strings are retained;
an excess field is rejected before constructing a large token list. Vertex and
triangle lists grow only within their budgets, then convert to the existing
contiguous arrays. Normal records consume no retained normal-value buffer.
Conversion temporarily retains both list storage and array storage; these budgets
are logical byte/record limits, **not** an exact heap/RSS quota. List capacity,
allocator overhead and stream buffering add memory. Allocation failure remains
possible. Work is bounded by input size; there is no wall-clock deadline.

Malformed records, invalid indices/numbers and record/configuration limits use
R4010. Parse messages identify the one-based OBJ line. File open/read errors and
byte/line budget violations retain stream error R4016. An oversized line's error
does not promise a recoverable line boundary. Input is closed on success or error
by `with`; no output file is opened by `read_obj`.

Input follows the existing file stream's OS permission/symlink rules and can
block on special files. The importer is not a filesystem sandbox. Reading while
another process changes the file does not provide snapshot consistency.
