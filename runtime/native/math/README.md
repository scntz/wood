# Native binary64 trigonometry

`musl/` contains six **unchanged** source files from the official musl 1.2.5
[release archive](https://musl.libc.org/releases/musl-1.2.5.tar.gz):
`src/math/{sin,cos,__sin,__cos,__rem_pio2,__rem_pio2_large}.c`.
Archive SHA-256: `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`.
Their Sun Microsystems permission notices and the upstream COPYRIGHT file are
preserved. This is vendored mathematical code, not a musl runtime dependency.

The sole local `musl/libm.h` is Wood's small freestanding adapter. It renames
symbols to private static functions, selects direct binary64 evaluation and
omits the unused long-double table. Its checked floor/scaling helpers apply only
to bounded internal argument-reduction operands. They are not general math APIs.
The kernels, coefficients and full-range base-2^24 argument reduction are intact.
`math.inc` adds Wood's R4027 nonfinite-input checks. There is no libm call,
allocation, OS service, C++ runtime, or approximate large-input fallback.

Compile with contraction disabled, without fast-math, under the normal
round-to-nearest environment. Changing foreign floating-point control state is
outside the trusted C ABI contract. Nearly rounded results need not be bitwise
identical to every platform's libm. Numerical qualification must include
subnormals, signed zero, quadrant boundaries and the full finite exponent range.
