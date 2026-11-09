# Publishing packages

Publishing is free. A public Git repository is the only infrastructure a package needs.

## Commands

| Command | Purpose |
| --- | --- |
| `wood init --lib [PATH]` | Create a library with tests, a README and a `.gitignore` |
| `wood test` | Run `tests/test_*.wd` |
| `wood package validate` | Apply the installer's rules and compile every module |
| `wood package api` | Print the public interface as JSON |
| `wood package pack [-o DIR]` | Write a deterministic `.woodpkg` and its SHA-256 |
| `wood package prepare` | Check release readiness and print the next commands |
| `wood package listing [--tag vX.Y.Z]` | Print a catalog entry for a pushed tag |
| `wood package catalog validate DIR` | Check catalog entries |
| `wood package catalog build DIR` | Generate `index.json` |
| `wood package catalog verify DIR` | Fetch each pin and compare its real manifest |

Nothing here pushes, tags or uploads on your behalf.

## Minimum package

| Requirement | Why |
| --- | --- |
| `wood.json` at the package root | Identity: `name` and an exact `version` |
| At least one public module | Otherwise there is nothing to import |
| Portable ASCII file names | The installer rejects anything else |
| A `LICENSE` file | Strongly recommended; `validate` warns without it |
| `tests/test_*.wd` and a README | Recommended; `validate` warns without them |

Names beginning with `_` are private. A `wood-source.json` file is reserved for the
installer.

## Dependencies

Paths stay inside the package. Give a dependency a `source` to have it fetched for your
users, or commit it to bundle it:

```json
{"format":1,"name":"app_kit","version":"1.0.0","source":"src","dependencies":{
  "log":{"path":"vendor/log","version":"0.1.0","source":"wood@log"},
  "units":{"path":"third_party/units","version":"2.0.0"}}}
```

Add `vendor/` to `.gitignore` for fetched dependencies. A dependency cannot be both
committed and declared with a `source`.

## Versions and tags

Tag releases `vX.Y.Z` to match the manifest. Installing `#v1.2.0` from a package whose
manifest says another version fails with **D5104**. Never move a published tag: raise the
version instead. Existing locks keep their commit either way, but new users would receive
different code under the same name.

## Getting listed

The official catalog is a research snapshot. People can install your package with `wood add github@you/repository`. A fork that runs its own catalog can list it:
`wood package listing` prints the entry, in the format the
[catalog reference](https://github.com/scntz/wood/blob/main/catalog/README.md) describes.

Listing supports three kinds of source:

| Type | Use it for |
| --- | --- |
| `github` | A repository on GitHub, optionally a subdirectory |
| `git` | Any HTTPS Git host, including your own domain |
| `archive` | A `wood package pack` tar at any HTTPS URL, such as a release asset |

Git sources pin a commit and archives pin a SHA-256. Packed archives are stored without
compression, and archive sources may be at most 16 MiB.

## Official and community packages

| | Official | Community |
| --- | --- | --- |
| Name | `wood@name` | `yourname@name` |
| Maintained by | Published wood research snapshot | The author |
| Lives in | The Wood research repository | Anywhere public over HTTPS |
| Listing check | Reviewed pull request | Reviewed pull request and matching account |

Neither tier is a security audit. Official packages are held to the project's own testing
rules; their status, including experimental, is stated on each package page.
