# Known limitations

This page lists what wood does not do yet and what has not been tested.
The [support matrix](support.md) has the detail behind it.

## Platforms

- Releases exist for Windows x86-64 and Linux x86-64 with glibc. There are no macOS, ARM, 32-bit or musl releases.
- Testing covers Windows 11 and Debian 13. Other Linux distributions may work, but have not been tested.
- The installer tests redirect the user profile and registry, so a truly clean machine has not been tested.
- Each release's `qualification.json` records every check and failed attempt for its exact source commit.

## Security

- Windows installers and executables are not code signed, so Windows shows a publisher warning. Check `SHA256SUMS` first.
- wood is not a sandbox. A program, and every dependency it imports, runs with your permissions.
- A catalog listing is not an audit. Read the [trust model](packages.md#trust-model) and the [security policy](../SECURITY.md).

## Language and tools

- There are no version ranges, REPL, async syntax or raw-pointer FFI.
- `wood add wood@...` needs Git, and the wood repository must be reachable.
- Checking a file gets disproportionately slower with tens of thousands of top-level names. 50,000 took about 17 seconds in testing, against under 2 seconds for 12,500.

## Optional services

- TLS and HTTPS need an installed OpenSSL 3, and SQLite needs version 3.38 or newer. Neither is bundled.
- On Windows, wood loads OpenSSL only from System32 or an absolute `WOOD_OPENSSL_DIR`, and Windows does not include it.

## Official packages

The GUI, audio, image, process, log and config packages are the parts of wood most likely to change.

- `wood@gui` and device audio playback are Windows only.
- Audio recording, GUI accessibility and multiple-monitor DPI have not been tested.

## Python

Python packages and `--bundle-python` are optional, and a program that does not use them contains no Python.

- The bridge runs CPython 3.14 in your process. Wheels are native code that wood does not review.
- NumPy's behavior at interpreter shutdown is unresolved, so wood keeps the interpreter alive until the program exits.

## Editors

- The VS Code extension installs from a `.vsix` file, and is not on the Marketplace yet.
- It has been tested in VS Code on Windows, not on Linux, and not in other editors. `wood lsp` itself has protocol tests on both
  platforms. See [its known limits](editors.md#known-limits).

## Packages

The package catalog is a static list in the repository. There is no package registry or account service.

## Sanitizers

Address and undefined-behavior sanitizers run on Linux. ThreadSanitizer on the allocator, and sanitizers on Windows, have not been run.
