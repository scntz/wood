# Stability and compatibility

Wood is an open-source research project. This page records the compatibility goals and qualification
of the published releases. There is no scheduled release cadence or commitment to ongoing maintenance.
The latest distribution is 1.1.0, the language version is 0.4.0, and the native runtime ABI is 4.

## Wood 1.1.0 release evidence

The [qualification report](https://github.com/scntz/wood/releases/download/v1.1.0/qualification.json)
records 60 passing checks in 60 attempts for commit `67178a434024c9a9beb639935ce5c50057f7277d`.
It covers the Windows wizard and portable archive, Linux installer, diagnostics, local package
mirror, editor tooling, runtime and library checks, and Linux sanitizers. It also records the
pinned bootstrap seeds and SHA-256 hashes for the six distribution assets.

The report leaves clean-machine installation, package fetching from the live public repository,
GitHub Actions for the release commit, Windows sanitizers, allocator ThreadSanitizer qualification,
VS Code on Linux and other editors, audio recording, GUI accessibility, and multiple-monitor DPI
unqualified. Windows TLS requires OpenSSL 3, and Windows artifacts are unsigned. The results
apply to the tested release and environments.

## Criteria

### Language semantics and compiler correctness

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| A written language specification that matches behavior | Met | [Language reference](language.md), [grammar](grammar.ebnf), [metadata](language.json) |
| The compiler rebuilds itself with identical output | Met | Stages two and three byte-identical on Windows and Linux at every release build |
| Reference and native compilers agree | Met | The reference suite and IR comparisons in every release build |
| Native and browser execution agree | Met | `tests/playground`: the playground examples and sandbox programs give the same output. 67 more programs from the repository are compared too, and each remaining difference comes from a browser limit such as files or sockets |
| Checked integers and indexing cannot be bypassed | Met | Tested on every backend, including negative tests |
| Language freeze: one review of every feature, then no breaking change without a new major version | **Not yet** | The language is still numbered 0.4.0 |
| Independent testing at scale, such as fuzzing the parser and checker | Partly | [`tests/fuzz`](../tests/fuzz) mutates programs and untrusted input on both platforms, and builds whatever the checker accepts. Runs so far are minutes, not days, and the fuzzers are ours |

### Public API stability

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| Every public API is documented | Met | [API index](stdlib-api.md), [package API](official-packages-api.md), checked for staleness by `check-docs` |
| A written compatibility policy | Met | [Below](#the-compatibility-promise) |
| A review of names and signatures across libraries, before they are frozen | Met | [The review](#api-review), with one addition and one rename before release |
| Official packages reach 1.0 versions | **Not yet** | All are 0.x, and most are experimental |

### Standard library and package quality

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| Real implementations, documentation and tests for every official library | Met | Each has a test suite that runs in every release build |
| Known library gaps closed | Partly | Elementary math and 3D rendering were added for 1.0. See [the tiers](#library-tiers) |
| Decoders and parsers tested against malformed input | Met | Fuzzed: JSON, CSV, UTF-8, numbers, OBJ, HTTP, PNG, BMP and WAV. Negative tests for archives |
| Windows GUI, audio and Python are complete enough to rely on | **Not yet** | Experimental. Accessibility, DPI and recording are untested, and the [Python shutdown issue](python-distribution.md) is open |

### Runtime reliability

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| No memory errors in sanitizer builds | Partly | All 69 native tests pass on Linux, with AddressSanitizer, UBSan and ThreadSanitizer suites and the parser fuzzer under AddressSanitizer. None on Windows |
| Allocator behavior under concurrency | Partly | Earlier ThreadSanitizer work found and fixed a sharing-flag race. The 1.1.0 report leaves allocator ThreadSanitizer qualification open |
| Long-running programs are stable | Partly | `tests/soak.wd` ran 30 minutes on each platform, and memory stays flat on Linux. Days have not been tried, and the heap never returns memory to the OS |
| Uncaught runtime errors read well | Met | `tests/explanations.cmake`, including stack overflow, on both platforms |

### Installation and distribution

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| Reproducible release builds from a clean checkout | Met | `tools/release`: archives and installers byte-identical across two builds |
| Installs and uninstalls cleanly on a clean machine | **Not yet** | The installer tests redirect the user profile. No clean machine yet |
| Installers are signed | **Not yet** | Windows files are unsigned, so people see a publisher warning |
| More than one tested machine per platform | **Not yet** | One machine per platform so far |
| Continuous integration runs on real commits | **Not yet** | Workflows exist but don't run. All checks are local |

### Documentation and editor support

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| Documentation examples are tested | Met | `tests/documentation` runs the registered code blocks |
| Checks for stale links and API pages | Met | `tools/check-docs.cmake` |
| A language server with real diagnostics, navigation and rename | Met | `lsp_protocol` on both platforms |
| An editor extension tested on each platform | Partly | VS Code on Windows only |
| Error explanations cover the common errors | Met | [Every code](reference/explanations.md) of the compiler, runtime and command, and a test runs each example |

### Security and reproducibility

| Criterion | Status | Evidence or gap |
| --- | --- | --- |
| A published security policy with a private reporting route | **No longer** | [SECURITY](../SECURITY.md): there is no supported private reporting route or security update commitment |
| Dependencies audited, with none in the compiler | Met | The compiler has no third-party code beyond the attributed math routines. `npm audit` is clean for the extension |
| Packages pinned by commit and hash, with revocation | Met | Package manager suite, 351 checks |
| Secrets and private data kept out of the repository and releases | Met | `tools/release-audit.ps1`, and a scan of each release's files |
| An external security review of the compiler and runtime | **Not yet** | Not done |

## Library tiers

| Tier | Meaning | Libraries |
| --- | --- | --- |
| **Ready to stabilize** | Tested on both platforms with negative cases, with no known defects. Names may still change once at the API review | Core language runtime; `std.binary`, `collections`, `csv`, `fs`, `functional`, `geometry`, `io`, `iter`, `linalg`, `math`, `net`, `http`, `num`, `path`, `random`, `stats`, `task`, `time`, `utf8`; `wood_cli`, `wood_installer` |
| **Works, needs an outside dependency** | The same quality, and the dependency is yours to install | `std.tls`, `std.https` (OpenSSL 3), `std.sqlite` (SQLite 3.38+) |
| **Experimental** | Works and is tested, but the API and coverage are still moving | `wood_image`, `wood_log`, `wood_config`, `wood_process`, `wood_audio`, `wood_windowsgui`, `std.python` and `python@` packages |
| **Browser only** | Built separately, not part of a release | The [playground runtime](playground.md) |

## Which library for which job

| To build | Use |
| --- | --- |
| Command-line tools | `wood_cli`, `wood_process`, `std.env`, `std.io` |
| Files and data | `std.fs`, `std.path`, `std.io`, `std.csv`, `json_parse`, `wood_config`, `wood_log` |
| Network services and clients | `std.net`, `std.http`, `std.https`, `std.tls` |
| Databases | `std.sqlite` |
| Desktop applications | `wood_windowsgui`, Windows only |
| Images and graphics | `wood_image` |
| Audio | `wood_audio`: files everywhere, playback and recording on Windows only |
| Mathematics | `std.math`, `std.num`, `std.linalg`, `std.stats` |
| 3D meshes | `std.geometry` to load, build and transform, and the `scene` module of `wood_image` to render |
| Python libraries | `import python@numpy`, experimental |
| Concurrency | `std.task` |
| Shipping your application | `wood build`, `wood_installer` |

## API review

Every public name in the standard library and official packages was reviewed before 1.0. Two additions came out of it: `std.math` gained
`sqrt`, `sin` and `cos`, since it had `tan` and `atan` but sent people to `std.num` for the rest, and `abs`, matching `std.num`. `abs_float`
stays beside it, because the catalog pins a `wood_image` that calls it. These rules hold across the libraries:

| Rule | Examples |
| --- | --- |
| A float function has the plain name, and its integer form says so, since wood has no overloading | `pow` and `pow_int`, `std.stats.sum_int`, `sorted_int` |
| `parse` and `dump` work on text, `encode` and `decode` on bytes, `load` and `save` on a file path | `std.csv.parse`, `wood_audio.wav.encode`, `wood_image.codecs.load` |
| Time is in seconds as a float, unless the name ends in `_ns` | `monotonic`, `monotonic_ns` |
| A resource has `close`, and most have `closed`, so it works with `with` | `std.io`, `std.sqlite`, `std.python` |
| Limits are arguments, not hidden constants | `io.read_all(source, limit)`, `http.Limits` |

Two things break these rules, and stay because renaming them would break programs for little gain: `std.csv.read_table` takes text
rather than a path, and `std.num` and `std.math` both offer `sqrt`, `sin`, `cos` and `abs`. Both spellings remain in the published release.

## The compatibility promise

The 1.x compatibility policy guided the published releases. It sets expectations for comparing
those releases and does not impose obligations on independent forks.

- Programs that compile and run on 1.x keep doing so on later 1.x releases, unless they depended on a bug or undocumented behavior.
- Public names in the standard library are not removed or changed in meaning within 1.x. New ones may be added.
- Error codes keep their meaning. The wording of messages can improve, so tools should use codes and positions.
- Experimental libraries are outside this promise until they are labeled otherwise.
- Security fixes may break behavior that was itself the vulnerability, and the release notes will say so.

## What remains

These were left undone when the project stopped. The rows marked not yet, most important first:

1. **Clean machines and more hardware**, since an installer that has only run on one computer is the largest unknown.
2. **Running the workflows** on every commit, and testing `wood add` against the public repository.
3. **Longer fuzzing and soak runs, and sanitizers on Windows.** What has run so far found real defects, so more is likely to find more.
4. **The language freeze**, the point of no return, so it comes after the items above.
5. **Official packages at 1.0, with Windows GUI, audio and Python either finished or left out of the promise.**
6. **Code signing and an external security review**, which cost money.
