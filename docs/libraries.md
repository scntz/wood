# Create and share a wood library

> **The usual path is now `wood add` and `wood init --lib`.** See
> [how packages work](packages.md) and [publishing packages](package-publishing.md). This
> guide covers vendoring by hand with the export helper, which still works and needs no
> package commands.

wood packages use format-1 manifests, ordinary modules, local directories,
exact versions and verified source locks. No registry, account, server or install
script is needed. `wood package add` is **not** an implemented command.

## Author a library: Project A

Run `wood init northstar_measurements` where the directory is absent. Replace
its sample greeting sources/tests with your library and edit `wood.json`:

```json
{"format":1,"name":"northstar_measurements","version":"1.0.0",
 "source":"src","entry":"measurements.wd","tests":"tests","dependencies":{}}
```

Names/aliases are ASCII identifiers other than `std`. Prefer a publisher prefix
such as `northstar_measurements`; this reduces accidental collisions but reserves
no global namespace. Versions are exact `MAJOR.MINOR.PATCH`, without leading
zeros, ranges or prerelease suffixes. `author@library` imports are not supported.

The manifest accepts only `format`, `name`, `version`, `source`, `entry`, `tests`
and `dependencies`. Put author/maintainer, purpose, license identifier and platform
requirements in README; include the actual license texts. **`author` and `license`
are not manifest fields.** Credited, licensed distribution needs no schema change.

Public functions/records are exported by their names; `_` names are private.
Imports are not implicitly re-exported. There is no `export` keyword or module
initializer. An optional `entry` pointing at the public module makes normal
directory-oriented check/inspect/build commands usable.

The complete [Project A](../examples/library_author/wood.json) exports a `Reading`
record and `mean_kilograms` from [measurements.wd](../examples/library_author/src/measurements.wd).
It vendors an independently versioned [units dependency](../examples/library_author/vendor/northstar_units/wood.json).
Its [tests](../examples/library_author/tests/test_measurements.wd) check numerical
results, unchanged inputs and the empty-input error. From that library directory:

```sh
wood check . --diagnostics=json
wood lock .
wood deps . --locked
wood inspect . --locked
wood test . --locked --report=json
```

Review and commit manifest, sources, tests, licenses and lock. Building a
declaration-only entry creates an executable that does no application work;
it does not create a binary wood library. wood libraries are source packages.
Native archives are handled separately: see [native libraries](official-packages.md#native-libraries).

## Share immutable Git sources

Share a repository only when you intend to. Provide the full reviewed commit ID,
version and package subdirectory. Vendor transitive dependencies inside the
package; another developer cannot use a path to your private sibling checkout.
Fetch/clone with normal Git tooling, review identity/license/contents, and ensure
the chosen objects are available locally. Branches/tags may move. Git identity
and unsigned hashes do not authenticate a publisher or make code safe.

The supplied [vendor helper](../tools/vendor-library.cmake) reads a **local**
repository and exports committed regular files to an absent directory. It needs
CMake 3.25+ and Git with `--no-lazy-fetch` support (Git 2.47/2.55 exercised).
Linux publication additionally uses GNU coreutils `mv` (part of the tested
Debian system). Those tools are needed only for acquisition, not compilation
or running anything. The helper is tested on Windows and Linux, not macOS.
It runs no checkout, filters, hooks, downloads or native build. Replacement refs
are ignored and lazy fetching is disabled using
[Git's documented controls](https://git-scm.com/docs/git). Unsupported Git options
fail before export; an older Git is not silently allowed to fetch.

After creating an application and its empty `vendor` parent:

```sh
cmake -DREPOSITORY=/absolute/local/library-repository \
  -DREVISION=FULL_REVIEWED_COMMIT_ID \
  -DDESTINATION=/absolute/application/vendor/northstar_measurements \
  -P /absolute/wood-manuals/tools/vendor-library.cmake
```

Replace the example paths and supply the full lowercase 40- or 64-character
commit ID. For a monorepo add `-DDIRECTORY=path/to/library`. That tree must contain
`wood.json`. PowerShell can use the same arguments on one line. Installations include the helper under `share/wood/tools`.

`wood-source.json` records commit/tree IDs and each file's SHA-256/size. Identical
objects produce identical receipt bytes. Raw blob copying avoids checkout
filters and archive extraction. The helper rejects mutable refs, traversal,
links, submodules, case collisions (including directories), Windows device names,
existing destinations and linked destination ancestors. Paths are portable ASCII,
at most 240 characters. Limits: 4,096 files and 128 MiB of blob bytes, not a whole
process-memory or hostile-repository sandbox limit. The receipt name is reserved.
Executable bits are not preserved; source packages have no executable hooks.

Use a private, non-linked parent without concurrent changes. Failed staging is
retained; publication refuses overwrite. This is not race-free filesystem
containment. Arbitrary downloaded archives are **not** supported by this helper.
If sharing an archive separately, provide a trusted checksum, inspect members
and use a safe extractor. Release compiler archives have their own manifests.

## Consume it normally: Project B

Run `wood init warehouse_report`, create `vendor`, and export the reviewed
library there. Replace obsolete greeting tests when replacing the scaffold.
Declare the application's alias and an exact version:

```json
{"format":1,"name":"warehouse_report","version":"0.1.0","source":"src",
 "entry":"main.wd","tests":"tests","dependencies":{
 "metrics":{"path":"vendor/northstar_measurements","version":"1.0.0"}}}
```

```wood
import metrics.measurements as measurements

values = [measurements.Reading("first", 1000), measurements.Reading("second", 2000)]
print(measurements.mean_kilograms(values))
```

This prints `1.5`. Project B sees `metrics`; the library sees its own `units`
alias. Transitive aliases are not implicitly visible to callers. Exported records
keep their defining module's nominal identity. Write a consumer test that
imports and asserts the real API, then:

```sh
wood check warehouse_report
wood lock warehouse_report
wood deps warehouse_report --locked
wood inspect warehouse_report --locked
wood test warehouse_report --locked --report=json
wood run warehouse_report --locked
wood build warehouse_report --locked -o warehouse-report
./warehouse-report
```

Use `warehouse-report.exe` on Windows. The
[automated walkthrough](../tests/community_packages.cmake) creates independent
projects, exports a committed library, moves Project B alone, makes Project A
unavailable, checks a three-package lock, runs tests and a standalone executable,
and rejects tampering. Commit vendor files, receipt, manifest and reviewed lock
with Project B. It then builds offline after relocation without the original
repository, Git or CMake. Builds still need the wood profile's LLVM/linker or C++.

Source locks hash manifests and transitive `.wd` files, **not** arbitrary
README/license/receipt/native-archive bytes. Review and retain those alongside
the lock. Locks are neither signatures nor complete provenance.

## Deliberate upgrades and local development

1. Review the new commit, license, API and exact version.
2. Export into a new absent version directory; retain the old copy.
3. Change dependency path and version together. Expect the old lock to fail.
4. Intentionally run `wood lock`, review its diff, then check/inspect/test/build.
5. Remove an old vendor directory only after checking that nothing references
   it and it has no unique work. wood does not remove it automatically.

A reviewed relative path to another checkout also works during development.
Refresh locks only for intentional edits; vendor dependencies before sharing.
There is no automatic upgrade solver or install script.
