# Roadmap

These are open research directions preserved from the work on wood 1.1.0. They carry no release dates
or implementation commitments. For the released behavior and evidence, see [stability and compatibility](stability.md).

## Before the language freeze

The language is still numbered 0.4.0. Freezing it means reviewing every feature once, then making no breaking change without
a new major version. Before that:

- Install and test on clean machines and more than one computer per platform.
- Run continuous integration on every commit. The workflows in `.github/workflows` were written for this, but rarely run.
- Fuzz and soak for days rather than minutes, and run sanitizers on Windows.
- Bring the official packages to 1.0, and either finish the Windows GUI, audio and Python bridge or leave them outside the
  compatibility promise.
- Code signing, and an external security review of the compiler, the runtime and the registry.

## Language

- Constraints on generic parameters, so generic code can require operations such as comparison.
- Generators, and later async iteration and coroutines, built on the existing iterator protocol.
- Clearer separation of bytes and text, with explicit encodings.
- User-defined resources that work with `with`, instead of passing foreign handles around as integers.
- Richer errors, with context and tracebacks.

## Tools and packages

- Reporting several errors at once, with spans and notes.
- Source maps for native builds.
- Version ranges and a resolver, on top of today's exact versions.
- A community package registry with authenticated metadata.
- A build cache that invalidates on toolchain, runtime and option changes.

## Libraries and interop

- C bindings with explicit ownership, for libraries that hand back resources.
- More OS, filesystem, network, time and serialization functions, driven by real applications.
- Structured concurrency and cancellable waits on top of `std.task`.
- For the Python bridge: bundled runtimes, more Python versions, typed signatures and zero-copy arrays.

## Platforms and performance

- macOS and ARM releases.
- Closing the gap with C++ where hot loops update dictionaries and records. 1.1.0 closed much of it for lists and arrays,
  and the [benchmarks](performance.md) show where wood still loses.
- Measuring memory, compile time and binary size, not only run time.
