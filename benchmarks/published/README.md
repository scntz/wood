# Published measurements

The results behind the site's [benchmarks page](../languages/README.md), from one machine on one date each:

- `languages-windows-20261104-0081ad5.json`: nine programs in wood, C++, Rust, Go, Java and Python on Windows 11.
- `languages-linux-mandelbrot-20261104-0081ad5.json`: the Mandelbrot comparison repeated on Debian 13.

Each file keeps every sample, the flags and the tool versions, exactly as recorded. Home directories are redacted. New
experiments go in `benchmarks/results/`, which Git ignores. See [performance](../../docs/performance.md) for how to measure.
