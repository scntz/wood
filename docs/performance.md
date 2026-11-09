# Performance

## wood against other languages

Nine small programs, written the natural way in wood, C++, Rust, Go, Java and Python, and built as optimized release
builds. wood lost clearly to C++ in 7 of the 9, and to Rust in 7 of the 9. It keeps up in floating-point loops and startup,
is about 1.5 times slower than C++ for function calls and text processing, and 5 to 34 times slower where a hot loop
updates lists, records, arrays or dictionaries. These are 1.0.0 numbers: 1.1.0 made the list and array loops 2 to 13 times
faster, as the [changes below](#changes-that-made-a-difference) show, and dictionaries are unchanged.

The machine was an x86-64 PC with 4 cores and 8 threads, running Windows 11 with nothing else heavy. wood ran at `-O2` with a
compiler build that is byte-identical to the compiler in 1.0.0. The others were MSVC 19.51, Rust 1.99.1, Go 1.27.3,
OpenJDK 25 and CPython 3.14.7. Each number is the median of 11 runs of the whole process, in milliseconds, with the range in
brackets. Every language printed the same answer every time.

| Workload | wood | C++ | Rust | Go | Java | Python |
| --- | --- | --- | --- | --- | --- | --- |
| Function calls | 753 (745-791) | 495 (485-550) | 388 (382-430) | 561 (535-710) | 706 (700-718) | 22966 (22899-25172) |
| Sieve | 1340 (1323-1355) | 257 (253-283) | 249 (245-270) | 257 (252-281) | 333 (327-361) | 6013 (5963-7669) |
| Mandelbrot | 129 (127-132) | 138 (135-155) | 136 (132-143) | 146 (144-152) | 312 (308-320) | 9102 (9046-9212) |
| N-body | 2386 (2367-2401) | 101 (99-103) | 53 (48-60) | 107 (102-120) | 191 (188-199) | 10773 (10708-11974) |
| Matrix | 2819 (2781-2899) | 82 (79-89) | 45 (41-50) | 98 (93-108) | 158 (153-172) | 24246 (24097-26282) |
| Quicksort | 2972 (2948-4021) | 528 (526-578) | 494 (490-506) | 542 (533-559) | 580 (570-663) | 25120 (24841-30290) |
| Dictionary | 2163 (2146-2584) | 383 (379-592) | 1148 (1106-1292) | 552 (528-782) | 429 (405-592) | 1516 (1474-9993) |
| Text processing | 496 (489-620) | 361 (355-374) | 372 (360-440) | 370 (366-381) | 662 (642-970) | 1124 (1116-1150) |
| Startup | 12 (10-16) | 14 (12-16) | 16 (13-24) | 18 (16-189) | 84 (81-99) | 44 (42-46) |

In Mandelbrot, wood took about 7% less time than MSVC. That's a small gap: two earlier, noisier runs overlapped with C++.
On Debian 13, on the same hardware, wood's median of 123.2 ms (120.0-126.9) was below `g++ -O3` at 128.4,
`g++ -O2` at 126.0 and `clang++ -O3` at 125.9, but the ranges overlap, so it isn't a clear win. Startup overlapped with C++ too.

Memory use, compile time, binary size and other operating systems weren't measured.

Every sample and flag is in
[`benchmarks/published/languages-windows-20261104-0081ad5.json`](../benchmarks/published/languages-windows-20261104-0081ad5.json).
[`benchmarks/languages`](../benchmarks/languages/README.md) has the programs, the rules they follow, and how to rerun them.

## Changes that made a difference

**Indexing lists and arrays inline (1.1.0).** Each `values[i]` used to call the runtime to check the index, and each
write called it twice more to check whether the collection was shared. The compiler now emits the bounds check and the
ownership check inline, calls the runtime only when a shared collection has to be copied, and reads a local list or array
without pinning it when the index calls nothing. Medians of 5 runs on the machine above, with the same arguments and
answers as the table:

| Workload | 1.0.0 | 1.1.0 | C++ in the table |
| --- | --- | --- | --- |
| Sieve | 1349 ms | 647 ms | 257 ms |
| Quicksort | 2961 ms | 1012 ms | 528 ms |
| N-body | 2356 ms | 178 ms | 101 ms |
| Matrix | 2871 ms | 495 ms | 82 ms |
| Dictionary | 2147 ms | 2153 ms | 383 ms |

Dictionaries still call the runtime for every lookup, so that row didn't move.

**Writing to a collection that the same statement reads.** Before 1.0, `values[i] = values[i] + 1` copied the whole collection
on every write. For a list of 40,000 floats that took 546 ms instead of under 1 ms; for an array of the same size, 6625 ms.
1.0.0 updates the element in place, and both forms take 2 ms or less.
[`self_update.wd`](../benchmarks/languages/self_update.wd) reproduces this.

**Small allocations on Linux.** The runtime used to ask the OS for every allocation, even 16 bytes. Blocks up to 64 KiB now
come from 1 MiB chunks, recycled by size, and reference counts skip atomic instructions until a second thread starts.
Semantic analysis also stopped rebuilding module-sized tables at each function and scanning every declaration on each
member access. The unoptimized compiler on Debian, median of 5 runs:

| `wood` command, on Debian | Before | After |
| --- | --- | --- |
| `check` a one-line script | 25 ms | 4 ms |
| `check examples/media_desk`, 18 modules | 7037 ms | 306 ms |
| `check` the compiler itself, 38 modules | 42211 ms | 2891 ms |

On Windows, where `HeapAlloc` was already reasonable, checking the compiler went from 5313 to 2917 ms and smaller commands
didn't change measurably. `tools/bench-dx.ps1` reproduces the Windows numbers.

The trade-off: freed small blocks stay with the process, so memory stays at its high point, and threads share one heap lock.

## Other harnesses

`benchmarks/` also holds the harnesses used while building the compiler. Each compares wood with a C++ program that uses the
same checked runtime and algorithm, so it measures what the compiler adds, not the fastest possible C++.

| Harness | Measures |
| --- | --- |
| `run.py` | The sieve, dictionary, string length and callback programs against C++ and Python. Build the matching `wood_bench_*` targets with `-DWOOD_BUILD_BENCHMARKS=ON` |
| `run_performance.ps1` | Text, lists, dictionaries, arrays, closures and lazy pipelines against an earlier compiler you keep |
| `run_native_computing.ps1`, `run_lazy_pipelines.ps1`, `run_string_buffers.ps1` | Arrays, iterator pipelines and string building |
| `run_backend.ps1` | SQLite, TLS and task overhead |
| `run_compiler_intelligence.ps1` | `wood check` and `wood inspect` on large generated programs |

For example, on a Linux source build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWOOD_BUILD_BENCHMARKS=ON
cmake --build build --target wood wood_bench_reference
python benchmarks/run.py --wood build/wood --cpp build/wood_bench_reference --output benchmarks/results/local.json
```

On Windows, use a Visual Studio x64 developer shell, `--config Release`, and the `build/Release/` executables.

## Measuring well

- Close other heavy programs, and keep the machine plugged in.
- Use medians of several runs, keep the ranges, and only call something faster when the ranges don't overlap.
- Check every program's output, so a broken build can't look fast.
- Sanitizer builds are for finding bugs, not for timing.
- Say which machine, compiler versions and flags you used, and keep the raw samples.
