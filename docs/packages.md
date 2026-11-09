# How packages work

One command manages every kind of dependency. A small reviewed catalog gives wood packages
short names, but the code stays with its authors. Direct installation from GitHub never needs
the catalog, and Python packages come from PyPI.

```sh
wood add wood@audio
wood add john@gui#1.2.0
wood add github@john/externalgui#v1.2.0
wood add python@numpy
wood update
wood list
```

## Catalog for the 1.1 release

Download [wood-catalog.json](https://github.com/scntz/wood/releases/download/v1.1.0/wood-catalog.json)
with `SHA256SUMS` from the same release and verify its hash. The downloadable 1.1.0 compiler
needs this file for official package lookup. Pass `--index PATH` when adding a catalog package,
or set `WOOD_PACKAGE_INDEX` to the absolute file path before using catalog commands.

```sh
wood add wood@image --index ../wood-catalog.json
```

The updated index points to the same eight package versions as SHA-256 pinned release archives.
An existing project with its dependency files can still build. If a lock requires restoring a Git
source that is no longer reachable, review and re-add that exact package version using the new index
in a fresh project or worktree. Review the resulting manifest and lock before adopting the change.

## Commands

| Command | Purpose |
| --- | --- |
| `wood add SOURCE` | Add a package and pin it in `wood.lock` |
| `wood install dependency SOURCE` | The same command under its long name |
| `wood install` | Restore and verify everything in `wood.lock` |
| `wood update [ALIAS\|python@NAME]` | Move dependencies to their latest version |
| `wood remove ALIAS\|python@NAME` | Remove a dependency |
| `wood list` | Show what this project depends on and how each is pinned |
| `wood info owner@package` | Print a catalog entry |

Options: `--as ALIAS` chooses the import name, `--offline` never touches the network,
`--update` allows `add` to change a pinned commit, `--index FILE` uses a local index, and
`--project PATH` selects the project. Git and catalog sources need `git` on `PATH`, and
archive and Python sources need `curl`. These commands belong to the wood-written native
CLI; the C++ reference driver reports that they are unavailable. Browse packages in
[`catalog/`](../catalog/README.md): there is deliberately no `wood search`.

## Names and sources

| Form | Meaning |
| --- | --- |
| `owner@package` | Look up the latest version in the catalog |
| `owner@package#1.2.0` | Look up that exact catalog version |
| `github@owner/repo` | The highest `vX.Y.Z` tag, otherwise the default branch |
| `github@owner/repo#v1.2.0` | That tag or branch |
| `github@owner/repo#FULL_COMMIT` | That exact commit |
| `python@name` | The latest version on PyPI with a compatible wheel |
| `python@name#1.2.3` | That exact version |

`github@` sources are validated names, never arbitrary URLs. The URL is always built as
`https://github.com/owner/repo.git`.

The `wood` owner is reserved for official packages, and `python` and `github` for their
providers. Everyone else is a community publisher, and a community GitHub entry must live
under the account it names, so only the owner of `github.com/john` can publish as `john@...`.

## How a name resolves

1. **The index.** Use the release index via `WOOD_PACKAGE_INDEX` or `--index` with the downloadable
   1.1.0 compiler. A compiler built from the current source can also read `catalog/index.json` from
   the project repository, with a cached or bundled copy as a fallback.
2. **The pin.** Every listed version names an exact commit or SHA-256.
3. **The source.** The package comes from GitHub, any HTTPS Git host or an HTTPS archive.

For independent packages, use `github@owner/repository` or point `WOOD_PACKAGE_INDEX`
at an index you maintain.

## What gets recorded

`wood.json` keeps using the existing format. A dependency only gains an optional `source`:

```json
{"format":1,"name":"app","version":"0.1.0","source":"src","entry":"main.wd",
 "dependencies":{"gui":{"path":"vendor/john/gui","version":"1.2.0","source":"john@gui"}}}
```

`wood.lock` keeps its content inventory and adds a `remote` section:

```json
{"root":"vendor/john/gui","source":"github@john/externalgui","identity":"john@gui",
 "requested":"","commit":"FULL_COMMIT_ID","tree":"FULL_TREE_ID","catalog":"INDEX_COMMIT",
 "receipt_sha256":"HASH_OF_wood-source.json"}
```

Compilers ignore `remote` when checking content, so projects without external packages
are unchanged and existing locks stay valid. `wood lock` preserves it.

Each installed package gets a `wood-source.json` receipt listing the size and SHA-256 of
every file. `wood install` compares that receipt with the lock and every file with the
receipt, including files that are not source code.

## Python packages

`wood add python@numpy` provisions everything it needs, and nothing outside the project and
wood's cache. A project without `python@` dependencies never touches any of this.

1. It downloads a pinned CPython 3.14 from python-build-standalone, once, and checks its
   SHA-256 before extracting it into wood's cache. It never changes a system Python.
2. It creates an isolated environment at `.wood/python/env`.
3. It asks pip to resolve the package and all its dependencies, using wheels only, so no
   package's build code runs.
4. It records each exact version and wheel SHA-256 in the `python` section of `wood.lock`, per
   platform, then installs with `--require-hashes`. It accepts wheels only from
   `files.pythonhosted.org`.

```text
import python@numpy as np
import std.python as py

with np.call("array", [py.value([1.0, 2.0, 3.0])]) as values:
    print(py.to_float(np.call("sum", [values])))
```

`wood run` and `wood test` use the project's environment. `wood install` restores it from the
lock, offline if the wheels are cached. See [Python interoperability](python-distribution.md)
for what the bridge does and does not do, and for shipping an application without requiring
Python.

## Conflicts and cycles

A package appears once per dependency path, nested inside the package that needs it. Two
paths may share the same commit. If they need different commits of the same source, the
install stops with **D5106** and names both requesters. wood has no version-range solver,
and that is deliberate: versions are exact and changes are reviewed.

## Trust model

**What wood guarantees**

- Installing runs no package code. There are no install scripts or hooks, and no Git
  checkout, filter or submodule step.
- Stored Git credentials are not used: credential helpers and prompts are disabled, so a
  private repository simply reports as unavailable.
- Paths are checked before anything is written. Symbolic links, submodules, traversal,
  case collisions, reserved Windows names and the receipt name are rejected.
- Packages are validated and compiled in a staging directory. A rejected package leaves
  the project unchanged.
- Tags are resolved once and never trusted again. Git verifies object integrity, with
  `transfer.fsckObjects` enabled and `git fsck` run on the cache. A corrupted cache is
  quarantined and refetched, or rejected when offline.
- Archive sources require a pinned SHA-256 and accept only plain `ustar` files without
  links, such as the `.woodpkg` that `wood package pack` writes. They are parsed in wood, never
  by an external extractor.
- A version revoked in the catalog cannot be added. Restoring a project that already pins
  one prints a warning and still works, because the decision belongs to that project.
- Python wheels are verified by hash, and no setup script runs at install time.

**What wood does not guarantee**

- A listing is not a security audit. Imported code runs with your program's permissions,
  and wood provides no sandbox.
- Catalog entries are not cryptographically signed. The trust root is reviewed pull
  requests, Git history and the commit pins in your own lock.
- Locks detect change; they do not authenticate authors.
- Resolving a name with `--offline` and no lock trusts what is in the local cache.
- Python packages are native code that wood does not review. Wheels can run anything when
  imported, and PyPI's own trust model applies.
- A VERIFIED mark on a catalog entry records that one audit of one version happened. It is
  not a guarantee of safety.

## Limits

Packages may hold 4096 files and 128 MiB, with each file at most 16 MiB. Archives may be
at most 16 MiB. File names must be portable ASCII, so a repository with spaces or Unicode
in its paths is rejected. A graph may hold 128 packages. Private repositories are not
supported.

## Diagnostics

| Code | Meaning |
| --- | --- |
| `D5101` | Invalid source, option or project state |
| `D5102` | Network failure, rate limit, or an unavailable repository |
| `D5103` | The reference, version or commit does not exist |
| `D5104` | Malformed package, manifest or catalog entry |
| `D5105` | Integrity failure: tampering, corruption or a digest mismatch |
| `D5106` | Version conflict, dependency cycle or alias conflict |
| `D5107` | Unsafe content: links, traversal, unsafe names or URLs |
| `D5108` | Not available offline |
| `D5109` | Not in the catalog |

`tests/package_public_network.cmake` runs the whole workflow over the real network, against any small public repository
you point it at.
