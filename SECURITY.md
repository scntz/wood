# Security

wood has had no external security audit and makes no production-security guarantee.

## Security maintenance

Wood is a research project. Upstream provides no scheduled security updates, private reporting
channel, or response commitment. Independent forks should publish their own policy and reporting
route. The rest of this page describes the security boundaries of wood 1.1.0.

## Programs

A wood program runs with the full access of the user who launches it. There is no sandbox and no language-level isolation
of OS capabilities, so do not run untrusted programs outside an OS sandbox.

Integer overflow, division by zero, failed assertions, invalid numeric conversions, zero range steps, out-of-bounds indexes
and failed file, path or stdin operations produce runtime errors. Catching an error does not roll back earlier writes or
mutations. Whole-file reads and whole-document JSON and CSV need proportional memory, and truncating writes can leave partial
output after an error. Stack and resource exhaustion are not guaranteed to produce recoverable errors. Sanitizer runs add
another check, but don't prove memory safety.

## Compiler and editor

`wood check` and `wood lsp` analyze source without executing it or invoking a native compiler. Imported modules cannot run
top-level statements, and module paths must stay within declared roots. The frontend limits file size and nesting depth.
`wood lsp` writes only copies of the embedded standard library, into wood's cache, so go to definition has a file to open.

The VS Code extension does not run in untrusted workspaces. `wood.path` can be set only in user settings, so a project cannot
choose a program for you to run.

Process arguments go directly to OS process APIs with no command shell. On Windows, batch files are refused, because `cmd.exe` would parse their arguments, and a program named without a path is never taken from the current directory. Builds use unique temporary directories and publish
output only after success, and a source file cannot be selected as an output path. [Process limits](docs/cli.md#process-limits)
cap captured output and enforce deadlines. They do not cap inherited output, program memory or file writes. Filesystem operations are
not race-free against another process changing the same directories.

## Packages

[The trust model](docs/packages.md#trust-model) is the full statement. In short:

- Installing runs no package code. There are no install scripts or hooks, and no Git checkout, filter or submodule step.
- Stored Git credentials are not used, and only HTTPS sources are accepted, or a local mirror you name.
- Symbolic links, submodules, path traversal, case collisions and reserved Windows names are rejected before anything is written.
- Packages are validated in a staging directory, and archives require a pinned SHA-256. Python wheels are verified by hash.
- A SHA-256 lock detects changes against an inventory you reviewed. It does not authenticate authors, or defend against
  someone who can rewrite the lock. It does not cover native archives linked with `--link-library`.
- A catalog listing is not an audit, and catalog entries are not signed. Dependencies are trusted code that runs with your
  program's permissions.

## Release files

The Windows installer and executables are **not code signed**. Verify downloads against `SHA256SUMS` from the release page.
That detects corruption and tampering after upload. It does not protect against a compromised GitHub account, because the
checksums are published in the same place.

Setup verifies the whole payload before creating a destination, and verifies each file again while copying. It uses
`versions/<manifest-sha256>/` directories and does not overwrite unknown files. `qualification.json` records the exact source
commit and every test attempt.

## Native code and optional services

External C declarations and `--link-library` archives are trusted native code. A signature or architecture mismatch can cause
undefined behavior, and foreign code is outside wood's arithmetic, lifetime and error guarantees.

SQLite and OpenSSL are trusted libraries, loaded only on use. SQLite values should use parameter bindings, because SQL text is
application code. TLS clients require chain and SAN verification with explicit PEM or OpenSSL-default trust roots. There is no
insecure fallback and no automatic revocation checking. On Windows, OpenSSL loads only from System32 or an absolute
`WOOD_OPENSSL_DIR`, and `PATH` is ignored.

Python support is experimental and optional. Wheels are native code that wood does not review, and they can run anything when
imported. PyPI's trust model applies.

## Data formats

JSON input rejects duplicate fields, malformed UTF-8, numeric overflow and excessive depth, and native parsing is bounded to
16 MiB. Dictionaries use a tree index, so they do not depend on string hashes resisting collisions. CSV quoting preserves data
and does not escape spreadsheet formulas. UTF-8 helpers validate scalars, and do not normalize text or fold case. Task
cancellation is cooperative, so it does not bound foreign code or computation that never checks.

## Test keys

The keys in `tests/fixtures/tls` are public, offline test identities. Never use them for deployment.
