# Comparing wood with other languages

The same small programs in wood, C++, Rust, Go, Java and Python. Results are recorded in
`benchmarks/published/languages-*.json`, one run each. They describe that run, not every
later compiler. [Performance](../../docs/performance.md) summarizes `languages-windows-20261104-0081ad5.json`.

## Workloads

| Workload | Exercises | Arguments |
| --- | --- | --- |
| `fib` | Recursive function calls and integer arithmetic | 40 |
| `sieve` | Writing a list of booleans | 30,000,000 |
| `mandelbrot` | Floating-point loops, no memory | 1000 points a side, 100 iterations |
| `nbody` | Reading and updating a list of records | 1,000,000 steps |
| `matrix` | Nested loops over numeric storage | 500 by 500 |
| `sort` | Hand-written in-place quicksort on integers | 5,000,000 values |
| `dict` | Short strings and the standard hash map | 5,000,000 keys, 50,021 distinct |
| `strings` | Formatting, joining, splitting and parsing | 500,000 numbers, 6 rounds |
| `startup` | Starting, printing a line and exiting | None |

The workloads were chosen before anything was measured, and none has been dropped.

## Rules

- **Same algorithm, same answer.** The script stops if any language prints something different, or if any run changes its answer.
- **Natural code for each language.** Each uses that language's normal containers and libraries, without language-specific tuning, unsafe code, threads or SIMD.
- **Optimized release builds.** `run.py` lists the exact flags in the result. C++ uses MSVC, the compiler on the measured machine.
- **Whole processes, timed from outside.** Times include startup and printing, so they include the JVM's startup and warm-up and Python's interpreter startup.
- **Medians, with ranges kept.** Each round runs every program once in a new random order, and the results file keeps every sample.
- **A win needs a clear gap.** The site only says wood beat C++ when wood's slowest run was faster than C++'s fastest.

## A defect these programs found

In an earlier preview, a statement that writes to a collection and reads the same collection on its right-hand side copies the whole collection on every write:

```wood
values[i] = values[i] + 1.0      # in that version, copies all of values each time
```

This applied to lists, arrays, dictionaries and fields of records in a list. The natural form of `matrix` at 500 by 500 would have taken an estimated 41 hours on that compiler, extrapolated from 48 seconds at 100 by 100. `sort` and `dict` did not finish within 120 seconds.

`1.0.0` has the fix. The current results use the compiler from commit `0081ad5`, recorded as `wood_source`, which is also the compiler in that release, and all the programs here are written the direct way. `self_update.wd` shows the difference: on that compiler its two forms take the same time, and on an earlier preview they do not.

```sh
wood run benchmarks/languages/self_update.wd
```

## The one test wood won

In the recorded run, wood clearly beat C++ in one of nine tests: `mandelbrot`, 128.7 ms (127.3 to 132.2) against 138.1 ms (134.9 to 155.5) with MSVC.

Treat this as a small gap, not a settled result. Two earlier, noisier full runs gave wood 132 ms (130 to 208) and 139 ms (128 to 150), and both overlapped with C++. wood's Mandelbrot code was byte-identical in the recorded run, the first of those runs and an earlier preview, so that difference between them is noise.

Since a win could also depend on the C++ compiler, `check_linux.sh` repeats it on Linux against `g++` and `clang++` on the same hardware. wood's median was lower there too, but the ranges overlap, so it is not a clear win: see `benchmarks/published/languages-linux-mandelbrot-20261104-0081ad5.json`.

## Run it

This needs a measured `wood`, plus Rust, Java and Python on `PATH`, Go, and Visual Studio with its C++ tools. It runs on Windows only, because of MSVC.

Close other programs first: ranges widen under load, and that can change a verdict. The recorded run took about 23 minutes.

```sh
python benchmarks/languages/run.py --wood /path/to/wood.exe --go /path/to/go.exe --out build/bench-new --result benchmarks/results/languages.json
```

When wood is not a released build, add `--wood-source COMMIT`: the version string does not identify the compiler, and the result needs the commit. `--out` must be a new directory. `--rounds` and `--only fib,sort` help when trying changes, but a published result needs all workloads and the default rounds. After recording a new file in `benchmarks/published/`, update [performance](../../docs/performance.md) to match.
