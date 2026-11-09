# Native computing

`array[T]` is a fixed-shape numeric value with its own contiguous storage, and views that share it cheaply. The element
types are `int` (signed 64-bit) and `float` (IEEE binary64). No Python or numerical library is needed. It covers numerical
work on the CPU; it isn't a NumPy clone or a full scientific stack.

Both backends support everything on this page: arrays, views, checked kernels, `sqrt`, `finite`, full-range `sin` and
`cos`, passing numeric buffers to C, and loading meshes. C callbacks run synchronously on the calling thread, and each task
has its own callback scope.

```wood
import std.num as num

values = num.from_list([1.0,2.0,3.0,4.0,5.0,6.0])
matrix = num.reshape(values,(2,3))
assert(matrix[1,2] == 6.0)
assert(num.shape(matrix) == [2,3])
result = matrix * 2.0 + 1.0
assert(result[0,0] == 3.0 and matrix[0,0] == 1.0)
```

## Shapes, indexing and iteration

`num.zeros(shape)`, `ones(shape)`, `full(shape,value)` and `ints(shape,value=0)`
construct initialized arrays. An integer shape means one axis; a tuple or
`list[int]` supplies multiple axes. Tuples and single integers avoid temporary
heap-allocated shape lists. Rank is 1..8. Dimensions are nonnegative and native
allocation/product/stride limits are checked; empty axes are supported. Arrays
cannot resize. Use lists when dynamic insertion/removal is needed.

One integer index is required per axis: `a[i]`, `a[i,j]`, or `a[(i,j)]`.
Negative indexes count from the end. Rank mismatches, bounds and element types
are checked. Array metadata is dynamic; rank/shape errors are runtime errors.
Index assignment and augmented assignment retain wood's established evaluation
order and rooted-path reacquisition rules. No unchecked indexing is exposed.

`len(a)` is the **total number of elements**, independent of rank. `num.rank(a)`
returns the number of axes, and `num.shape(a)` returns an owned shape list.
Iteration, comprehensions and `std.iter` visit scalar items in logical row-major
order, including transposed/sliced values. This differs from row-yielding APIs.
`num.to_list` explicitly returns a flat list; JSON conversion requires an explicit
conversion to a supported representation. Arrays compare shape and values for
equality; ordering is unsupported.

## Views and ownership

```wood
import std.num as num
values = num.from_list([1.0,2.0,3.0,4.0,5.0,6.0])
matrix = num.reshape(values,(2,3))
snapshot = matrix
transposed = num.transpose(matrix)
assert(num.shape(transposed) == [3,2])
assert(transposed[2,1] == 6.0)
matrix[1,2] = 99.0
assert(snapshot[1,2] == 6.0 and transposed[2,1] == 6.0)

reverse = values[::-1]
assert(reverse[0] == 6.0)
```

Copies, transposes and slices retain shared owned storage with independent value
semantics. They are **snapshot views**, not mutable aliases. A view remains valid
after the original variable leaves scope. Writing shared storage copies the view's
logical elements into private contiguous storage. Unique indexed views may retain
their strided representation without allocating. No stack/element reference escapes.

`a[start:stop:step]` slices axis zero using ordinary wood slice rules, including
omitted bounds and negative strides. `num.slice(a,axis,start,stop,step=1)` slices
another axis. `num.transpose(a,axes=[])` permutes axes; an empty axes list reverses
their order. `num.contiguous(a)` checks positive row-major layout. Singleton and
empty axes have the usual contiguous special cases.

`num.reshape(a,new_shape)` preserves logical element count. It shares a contiguous
input and materializes a strided input when needed. `num.flatten` reshapes into
one axis. `num.copy` explicitly creates private contiguous storage. A small view
can retain a large backing allocation; copying it releases that retention once
other owners leave scope. These are deliberate, documented storage semantics.

Read-only array indexing borrows its base internally when index evaluation cannot
structurally mutate it and the base has a safe lifetime; otherwise it snapshots
before evaluating the index. Tuple selections and member paths through them own
their selected value before reading: C++ tuple-selection references do not extend
the lifetime of a temporary tuple.
Vector operators can reuse unique temporary buffers. The compiler can move a
once-used owned array local/parameter into a final top-level return expression;
captures and mutable foreign borrows are excluded. Return expressions containing
closures or comprehensions are excluded conservatively: a comprehension can
evaluate one source occurrence repeatedly. No source `move` syntax is required.
An in-function error handler never observes a local consumed by this optimization.

## Arithmetic and numerical behavior

`+`, `-`, `*`, `/` operate elementwise on equal-shape arrays or an array and a scalar.
There is no implicit multidimensional broadcasting. Numeric promotions match
scalar arithmetic: float operands produce float arrays and division produces
float arrays. Array types themselves remain invariant. Integer operations and
reductions check overflow; division rejects zero divisors for produced elements. Existing live
input values remain unchanged on kernel failure.

`num.floats(a)` and `num.integers(a)` explicitly convert element types while
preserving shape. Float-to-int conversion truncates toward zero and checks finite
range per item (R4003), without changing the source. A same-type conversion shares
its existing value storage. Empty elementwise results perform no scalar divisions.

`num.sum` reduces all logical elements; `num.dot` requires equal-length vectors.
Float reductions use Neumaier compensated accumulation for finite intermediate
sums; nonfinite arithmetic follows IEEE propagation. Compensation is not a promise
of correctly rounded results for every input. Integer reductions remain checked.

`num.matmul` requires float arrays of shapes `(m,k)` and `(k,n)`. Its independent
output columns permit host vectorization while preserving each output's k order.
No fast-math, reassociation requirement or architecture-specific ISA is imposed.
Actual vectorization and performance must be established for the selected compiler.

### Scalar comparisons and normalization

`num.close(left,right,relative=1e-9,absolute=1e-12)` compares finite values using
`abs(left-right) <= absolute + relative*max(abs(left),abs(right))`, with a scaled
fallback when both the distance and bound overflow. It does not accept unequal
large values merely because both intermediates become infinity. Tolerances must
be finite and nonnegative. Equal infinities compare close; NaNs and unequal
nonfinite inputs do not. This is a binary64 tolerance test, not exact arithmetic.
`Vec3.normalized()` normalizes scaled components to avoid magnifying rounding of
subnormal lengths. Zero, nonfinite and unrepresentable original lengths still
raise R4010. `length()` itself remains a rounded binary64 magnitude.

### Numerical builtin spellings

Prefer `std.num`/`std.linalg` for application code. The following unreserved
fallback calls are also implemented and discoverable in `--language-info`.
Here `T` means `int` or `float`, and `shape` means the supported int/tuple/list
dimension value, not a new type annotation. Arguments are positional; these
primitives do not inherit wrapper defaults. Errors and ownership match the
contracts above.

| Call | Result / wrapper |
| --- | --- |
| `array_full(shape, value: T)` | `array[T]`; `num.full` and initialized constructors |
| `array_from_list(values: list[T])` | Rank-one `array[T]`; `num.from_list` |
| `array_shape(values: array[T])` | `list[int]`; `num.shape` |
| `array_rank(values: array[T])` | `int`; `num.rank` |
| `array_contiguous(values: array[T])` | `bool`; `num.contiguous` |
| `array_copy(values: array[T])` | Private contiguous `array[T]`; `num.copy` |
| `array_to_list(values: array[T])` | Flat `list[T]`; `num.to_list` |
| `array_reshape(values: array[T], shape)` | `array[T]`; `num.reshape` |
| `array_transpose(values: array[T], axes: list[int])` | `array[T]`; `num.transpose` |
| `array_slice(values: array[T], axis: int, start: int, stop: int, step: int)` | `array[T]`; `num.slice` |
| `array_sum(values: array[T])` | `T`; `num.sum` |
| `array_dot(left: array[T], right: array[T])` | `T`; `num.dot` |
| `array_matmul(left: array[float], right: array[float])` | `array[float]`; `num.matmul` |
| `array_affine(points: array[float], matrix: array[float])` | `(n,3)` output from affine `(4,4)` matrix; `la.transform_points` |
| `array_floats(values: array[T])` | `array[float]`; `num.floats` |
| `array_integers(values: array[T])` | `array[int]`; `num.integers` |
| `math_sqrt(value: float)` | `float`; `num.sqrt` |
| `math_sin(value: float)` | `float`; `num.sin` |
| `math_cos(value: float)` | `float`; `num.cos` |
| `math_finite(value: float)` | `bool`; `num.finite` |

`num.sqrt`, `sin`, `cos`, `finite`, `abs`, and tolerance-based `close` provide scalar
math support. Square root and trigonometric primitives validate their domains;
`close` validates finite, nonnegative tolerances. R4026 identifies array shape/layout
errors, R4027 native scalar math domain errors; R4001/R4002/R4006 retain overflow,
division and bounds meanings. wood library domain checks use R4010.

## Linear algebra and geometry

`std.linalg` provides `Vec3` arithmetic, cross/dot products, scaled length
calculation, normalization, identity and affine 4x4 transforms, bulk point
transformation and partial-pivoted linear-system solving for multiple right-hand
sides. Matrices use column-vector transform conventions. Point rows transform by
the transposed matrix. Transform composition is ordinary `num.matmul`.

```wood
import std.linalg as la
import std.num as num
points = num.reshape(num.from_list([1.0,2.0,3.0]),(1,3))
shifted = la.transform_points(points,la.translation(4.0,5.0,6.0))
assert(shifted[0,0] == 5.0 and shifted[0,2] == 9.0)
```

`la.solve(A,B,tolerance=1e-12)` takes square A and a rank-two B with matching rows.
It checks finite inputs, uses partial pivoting and rejects pivots below a threshold
relative to the input coefficient scale. It does not estimate condition numbers,
provide iterative refinement or replace a mature numerical solver for arbitrary
ill-conditioned problems. Inputs preserve their values.

`std.geometry` provides `Mesh` with `(n,3)` float vertices and `(m,3)` integer
triangles, validation, transformations, bounds, triangle area, area-weighted vertex
normals, a periodic torus generator and Wavefront OBJ export. OBJ uses one-based
`vertex//normal` triangle references. Normal generation rejects vertices with no
normalizable accumulated normal. Validation/normal computation precede opening
output; I/O failures after opening can still leave partial output.

```powershell
wood examples/mesh_torus.wd -- torus.obj 64 32
```

The application is entirely wood and applies nonuniform scaling, rotation and
translation, computes normals/bounds/area, exports the model and prints a typed
JSON summary. Its independent C++ test oracle verifies analytic coordinates,
unit normals, winding, edge incidence, torus Euler characteristic and area.

`geo.read_obj(path, *, max_bytes=16777216, max_vertices=1000000,
max_triangles=1000000, max_normals=1000000, max_line_bytes=4096)` imports a bounded
triangle-only OBJ subset. It accepts `v`, `vn`, bare `f` and `v//vn` faces with
positive or relative indices. Normals are validated but not retained; export
recomputes them. Unknown records, textures, materials and n-gons are rejected.
Read [OBJ importing](obj.md) for exact limits, errors and geometry-only round-trip
semantics. `examples/mesh_import.wd` loads, inspects, transforms and re-exports.

## Native numeric buffer interoperability

```wood
extern fn library_sum(values: array[float]) -> float
extern fn library_scale(mut values: array[float], factor: float)
```

A readonly array parameter maps to `(const double*, int64_t)` or
`(const int64_t*, int64_t)`. A mutable parameter maps to the corresponding writable
pointer and flat element count. Arrays must be contiguous; `num.copy` explicitly
materializes a view. Mutable arguments must be rooted lvalues. They detach from
snapshots before exposing storage and validate/reacquire their path around other
argument evaluation. A unique contiguous array can be borrowed without copying.

Pointers are valid only during the synchronous call; foreign code must not retain
them, access outside the provided count, mutate readonly data, invoke cross-thread
callbacks or throw across the C ABI. Native libraries remain trusted code, and an
incorrect declaration can corrupt the process. Mutable imports must be called by
name; function values currently cannot encode their mutable borrowing modes.
Multiple mutable arguments may alias if the caller supplies the same array.
Library-specific overlap rules remain the caller's responsibility.

This enables direct calls to compatible C ABIs with numeric buffers, without
writing C++. It does not generate headers, adapt int32 APIs, describe arbitrary
strided buffers, expose raw pointers, or implement a BLAS/LAPACK binding package.
