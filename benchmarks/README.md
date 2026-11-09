# Benchmark workloads

Use [performance and measurement](../docs/performance.md) for qualified results,
limits and older workload families. Published results describe their recorded
revisions, not every later compiler. Keep new binaries, payloads and raw logs in
ignored `build/` or `benchmarks/results/` directories.

[`languages/`](languages/README.md) compares wood with C++, Rust, Go, Java and Python, and supplies the site's benchmarks page.

## Native ownership and allocation profiles

`native_runtime.wd`, `reference_native_runtime.cpp` and
`reference_native_runtime.py` exercise seven deterministic workloads:

| Mode | Measured work |
| --- | --- |
| `list` | Append long strings, traverse them, retain the final result |
| `snapshot` | The same construction, plus an independently owned snapshot and mutation |
| `scalars` | Append and sum checked integer values; unmanaged-element control |
| `map` | Insert/look up integer-to-string mappings and collect ordered results |
| `pipeline` | Lazy string-length map/fold with a pinned input |
| `csv` | Serialize a 16-column table of long fields with CRLF records |
| `array` | Compute `numeric * 2.0 + 1.0`, reduce and retain the final array |

These are allocation/work profiles, **not timing benchmarks**. No clocks are read.
Inputs are prepared before the counters reset; final output formatting, disk
output and JSON reporting occur after measurement stops. The driver compares the
**entire payload** and checksum, not just a small accumulated number. Counts and
rounds are bounded (`1..10000`, `1..100`). Default replay: 2,000 elements, six
rounds and three fresh processes per variant/workload.

### Private instrumentation

`prepare_native_profile.cmake` copies the runtime to a new private directory,
instruments that copy, and builds an ABI-4 primitive object using the existing
freestanding C17 preparation tool. It refuses an existing output directory,
output nested inside the input runtime, missing inputs and linked source entries.
It never edits an installation or the runtime source. `native_profile.c` is
**not linked into ordinary wood programs**.

Supply an already qualified native compiler/installation, matching prepared
standard-library assets, LLVM 19 tools and the normal platform linker/SDK. See
[native installation](../docs/native-cli.md) for those prerequisites. For a
before/after comparison, preserve both runtime source trees. For example, extract
`runtime/native` from the baseline commit with `git archive` into a fresh private
directory; do not reset the checkout or replace installed compiler files.

On Linux, first set `qualified_wood` to the installed native `bin/wood`,
`qualified_data` to its `share/wood`, and `llvm_bin` to the LLVM tool directory.
From the repository root, create fresh private outputs:

```sh
mkdir -p build
work="$(mktemp -d "$PWD/build/native-profile.XXXXXX")"
mkdir "$work/baseline"
git archive 34591a1045dee9609524dc3e935fa374b0789998 runtime/native \
  | tar -xf - -C "$work/baseline"
before_source="$work/baseline"
```

The recorded baseline hash belongs to the preserved Core preview. For a later
optimization, explicitly choose and record that task's own before revision.

```sh
cmake -DCLANG=/usr/bin/clang-19 -DTARGET=linux-x86_64 \
  -DRUNTIME_DIR="$before_source/runtime/native" -DOUTPUT="$work/profile-before" \
  -P benchmarks/prepare_native_profile.cmake
cmake -DCLANG=/usr/bin/clang-19 -DTARGET=linux-x86_64 \
  -DOUTPUT="$work/profile-after" -P benchmarks/prepare_native_profile.cmake
```

In each **private copy** of the installation's `share/wood` directory, replace
only `native/managed.o` and the native `.ll` files with those from the matching
profile directory. Keep its standard-library/metadata assets unchanged. Build
the same workload with each copy:

```sh
for variant in before after; do
  cp -a "$qualified_data" "$work/home-$variant"
  cp "$work/profile-$variant/runtime/"*.ll "$work/home-$variant/native/"
  cp "$work/profile-$variant/managed.o" "$work/home-$variant/native/managed.o"
done
"$qualified_wood" build benchmarks/native_runtime.wd --backend llvm -O2 \
  --native-home "$work/home-before" --llvm-dir "$llvm_bin" --linker /usr/bin/ld \
  -o "$work/workload-before"
"$qualified_wood" build benchmarks/native_runtime.wd --backend llvm -O2 \
  --native-home "$work/home-after" --llvm-dir "$llvm_bin" --linker /usr/bin/ld \
  -o "$work/workload-after"
g++ -std=c++23 -O2 -DNDEBUG -Ibenchmarks \
  benchmarks/reference_native_runtime.cpp -o "$work/reference"
cmake -DBEFORE="$work/workload-before" -DAFTER="$work/workload-after" \
  -DCPP="$work/reference" -DOUTPUT="$work/replay" \
  -DCOUNT=2000 -DROUNDS=6 -DSAMPLES=3 -P benchmarks/run_native_profile.cmake
```

Windows uses the same CMake scripts with `TARGET=windows-x86_64`, native LLVM
executables, `lld-link.exe`, and a discoverable Windows SDK `kernel32.lib`. From a
Visual Studio developer shell, build the independent reference with:

```powershell
cl /nologo /std:c++latest /O2 /EHsc /utf-8 /permissive- /MT /Ibenchmarks `
  benchmarks/reference_native_runtime.cpp /Fo"$work/reference.obj" `
  /Fe"$work/reference.exe" /link /INCREMENTAL:NO
```

For the optional Python output oracle, add `-DPYTHON=/path/to/python` to the replay
command. It is optional tooling; normal compilation and execution never require
Python. Omit `AFTER`, `CPP` or `PYTHON` when unavailable. `BEFORE` is required.
Use a new output directory each time. The driver saves per-process stdout,
stderr, payloads and `profiles.json`, rejects nonzero exits/stderr, checks output
equivalence and requires native counters to repeat exactly across samples.

### Interpret the counters correctly

- `allocations` / `requested_bytes`: successful native platform allocation
  requests / sum of their requested sizes. Failed requests are not counted.
- `peak_additional_bytes`: native live requested-byte high-water mark above the
  reset baseline, **not process RSS**, page commitment or allocator overhead.
  Inputs stay live in these fixtures. C++ counts allocations made after reset
  through global `new`; its peak counts those allocations' live requested bytes.
  Its counter headers/allocator bookkeeping are excluded.
- `retains` / `releases`: calls with non-null owner pointers, including immortal
  no-ops and recursive helper calls; **not exact atomic reference-count updates**.
- `unique_managed_slots` / `shared_managed_slots`: typed element slots copied
  during sequence relocation/detachment. A slot may contain null owners. These
  identify avoidable exclusive-prefix callbacks versus required snapshot work.
- `-1`: metric unavailable in that implementation; never interpret it as zero.

The C++ reference uses immutable owning shared strings, checked indexed reads and
checked integer checksum addition. Its snapshot is copied immediately before
mutation; its `std::map`, vector growth/layout and fused numeric loop differ from
wood's implementation. Python uses an explicit list copy for value isolation,
arbitrary-precision integers within a bounded non-overflowing fixture, and ASCII
text so character counts equal wood byte counts. These are equivalent outputs on
the fixtures, not general language-semantic or implementation identity.

The CSV fixture isolates wide plain tables. Existing regression suites cover
quoting, errors and resource handling; this profile does not replace them. Array
inputs and results are exactly representable small binary64 integers. This does
not justify floating-point reassociation, fast math or general numerical parity.

### Compiler allocation profiles

Build `bootstrap/src/driver_main.wd` with a private instrumented runtime and a
private `bin/wood` plus `share/wood` installation layout. The private runtime's
normal-exit hook writes JSON counters to stderr. Run `emit-llvm` for a representative
application and the compiler itself. Swap before/after measurement executables
only at the **same private path**, use the same source/asset/output paths, require
valid JSON and successful emission, and compare complete emitted IR hashes.
Path differences and changing compiler flags can otherwise contaminate results.
Missing reports, failed emission and timeouts are invalid profiles.

### Allocation sites, sizes and lifetimes

For **single-threaded compiler emission only**, add `-DALLOCATION_SITES=ON` when
preparing a private native profile. Build the compiler against that copy, as
above. Normal exit writes the aggregate JSON line, a metadata line, then one JSON
line per allocation site. Preserve the complete stderr file. Require a successful
exit and reconcile the sum of site `allocations`/`requested_bytes` against the
aggregate before using a profile.

The tracer records:

- Exact C allocation-helper function, source file/line in the **instrumented copy**,
  and element stride for sequences. Families distinguish strings, typed sequences,
  dictionaries, JSON nodes, descriptors and raw platform requests. Stride alone
  does not identify a nominal wood type; inspect the allocation site too.
- The active wood **diagnostic** source/line/column. This is the last marked runtime
  location, not a reconstructed call stack or a guarantee of a fresh location for
  every internal allocation. Empty source/zero locations are unknown. Verify a
  suspected hot site against source and an isolated change.
- Request frequency/bytes, minimum/maximum size, per-site peak live bytes/count,
  final live bytes and frees. Site peaks occur at different moments: do not sum
  them and call the result global peak memory.
- Lifetime bins measured in **subsequent successful allocation events**, not time:
  `0..1`, `2..16`, `17..256`, `257..4096`, and `>4096` between allocation and free.
  An allocation still live at the exit checkpoint has no completed lifetime.

Tracing adds no allocation headers or measured allocator requests. Its fixed
metadata arrays consume **70,004,736 bytes** in the default build, plus small
globals/code; this overhead is excluded from tracked requested bytes and can
affect physical memory. It is not a release runtime or an RSS measurement tool.
The pointer registry permits at most 2,097,151 live requests (one empty slot is
reserved), 16,384 sites and 255 nonempty source-file identities of fewer than 512
bytes. Overflow, duplicate/unknown frees or size mismatches trap rather than
silently produce partial valid-looking results. The ABI-4 context bridge is
checked explicitly and exists only in the private runtime copy.

This mode does not support tasks, foreign concurrent callers, manual `bench_reset`
or general application profiling. It is designed for the existing compiler's
single-threaded emission path. No global allocator/pool, normal runtime ABI or
application API changes are involved.

The registry's collision/reuse, near-saturation deletion, all lifetime bins and
three invalid-accounting guards have an independent hosted oracle:

```sh
cmake -DCLANG=/usr/bin/clang-19 -DSOURCE_DIR="$PWD" \
  -DOUTPUT="$work/accounting-oracle" -P benchmarks/check_allocation_sites.cmake
```

This runs ASan/UBSan with assertions and preserves a new `run-*` directory each
time. The three negative invocations deliberately trap; successful positive
accounting and the expected failure kinds are checked separately.

### The same frontend compiled through C++

Generate C++ for `bootstrap/src/native_main.wd` using the qualified reference
compiler, then instrument its actual `main` **and Windows `wmain`** entries:

```sh
"$reference_wood" emit-cpp bootstrap/src/native_main.wd -o "$work/emitter.cpp"
cmake -DINPUT="$work/emitter.cpp" -DOUTPUT="$work/emitter-profile.cpp" \
  -P benchmarks/prepare_cpp_profile.cmake
g++ -std=c++23 -O2 -DNDEBUG -Ibenchmarks \
  -include "$PWD/benchmarks/compiler_cpp_profile.hpp" \
  "$work/emitter-profile.cpp" -pthread -ldl -o "$work/emitter-profile"
"$work/emitter-profile" "$stdlib_json" "$runtime_directory" linux-x86_64 \
  "$input_wd" "$work/captured.ll"
```

On MSVC use `/std:c++latest /O2 /EHsc /utf-8 /permissive- /MT /bigobj`,
`/FIbenchmarks/compiler_cpp_profile.hpp`, and `/STACK:8388608 /OPT:NOICF`.
The preparation tool refuses existing outputs and missing supported entries.
Validate **positive** counter reports; the wrong platform entry can otherwise
leave counters disabled. CMake normalizes generated text line endings; compare
normalized source when checking instrumentation against another preparation tool.

Counters begin before argument initialization and stop at normal process teardown;
the report is one JSON line on stderr. They count global-new requests and newly
tracked live bytes, excluding counter headers and allocator bookkeeping. This
uses the same wood-written semantic frontend/LLVM emitter through C++; it is not
a benchmark of a handwritten C++ compiler. The normal native driver has a wider
CLI/setup scope than the positional emitter. Compare before/after **within each
implementation**, using identical inputs/assets/output paths, and verify full IR
equality across implementations. Do not infer throughput or RSS ratios from the
absolute counts of different runtimes or STL growth policies.

### Timing and stopping rules

Do not time instrumented runtimes or infer speed from allocation/reference counts.
Under significant host load, collect only correctness and deterministic counters.
For a later timing phase, first establish suitable host conditions without
interrupting user processes; use uninstrumented builds, pinned tool versions and
flags, equivalent checked algorithms, warmups and repeated paired processes.
Report sample counts, spread, regressions and memory alongside execution time.
Do not combine historical timings with new counts to invent a speedup.

Each optimization needs a measured cause, unchanged public semantics, failure-path
and ownership tests, native/reference differential execution, sanitizer coverage
and staged self-builds where compiler/runtime ownership is affected. Stop a
milestone after its bounded candidate set is evaluated and one result is validated;
reject variants that trade away safety or introduce disproportionate memory/work
regressions. Record and investigate smaller tradeoffs, including retained capacity
and fixed cache costs, without claiming every metric improves. Leave larger
allocator, borrowing and numeric-kernel designs for
separately justified work rather than continually adding variants.
