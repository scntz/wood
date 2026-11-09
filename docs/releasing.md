# Building a release

Everything needed to build, test and assemble a wood release is in [`tools/release`](../tools/release). These scripts build
the files and publish nothing. [Publishing](publishing.md) is a separate step that needs the owner's approval.

## What you need

Nothing here is specific to one machine. Pass paths as arguments.

| Input | Windows | Linux |
| --- | --- | --- |
| Operating system | Windows 11 x86-64 | Debian 13 x86-64 |
| Build tools | Visual Studio with C++ and a Windows SDK, in an x64 developer shell, plus CMake 3.25+, Ninja, PowerShell 7, Git, Node.js and npm | CMake 3.25+, Ninja, Git, GCC, GNU ld, gzip, tar |
| LLVM | [LLVM 19.1.7 for Windows](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.7), checked against [`llvm-tools.json`](../tools/installer/llvm-tools.json) | Debian's `llvm-19` and `clang-19` |
| Seed compiler | `wood-core-windows-x86_64.tar.gz` from [v1.1.0](https://github.com/scntz/wood/releases/tag/v1.1.0) | `wood-core-linux-x86_64.tar.gz` from the same release |

[`inputs.json`](../tools/release/inputs.json) pins the seed archives by hash. To use a different seed, pass its hash
explicitly, and the build records that it was not the pinned release. The compiler rebuilds itself from this source and
must reach a fixed point, so its output doesn't depend on which working seed started it. On Linux, the release compiler,
installer template and runtime object came out byte-identical from different seeds. The archives do record the seed's
hash, so they differ if the seed does. The standard library, error explanations and runtime come from the checkout, not
the seed.

Node.js and npm fetch `@vscode/vsce` at a pinned version, so the extension step needs network access. Python is used only
by the optional site link checker and the Python tests. It is not needed to build.

## Build

Start from a clean checkout of the commit you intend to release. The scripts refuse a modified tree and an existing
output directory, and they keep their output outside the checkout.

On Windows, in a Visual Studio x64 developer shell:

```powershell
git clone https://github.com/scntz/wood.git wood-release
cd wood-release
git checkout COMMIT
./tools/release/build-windows.ps1 -SeedArchive SEED.tar.gz -LLVMDirectory LLVM/bin -Out C:/release/windows
```

On Debian:

```sh
git clone https://github.com/scntz/wood.git wood-release
cd wood-release
git checkout COMMIT
sh tools/release/build-linux.sh SEED.tar.gz /usr/lib/llvm-19/bin /absolute/release/linux
```

Each script builds three compiler generations and requires generations two and three to be byte-identical. It then builds
the installer tools from source, packages the distribution, and builds it twice to prove the bytes repeat. Windows also
builds the `.vsix`, which is not reproducible, and Linux builds the source archive.

## Test

Run the checks against those outputs. Every check runs even if one fails, and results are appended, so a failed attempt
stays in the record beside its rerun.

```powershell
./tools/release/qualify-windows.ps1 -Build C:/release/windows -LLVMDirectory LLVM/bin -Code PATH/TO/Code.exe
```

```sh
sh tools/release/qualify-linux.sh /absolute/release/linux /usr/lib/llvm-19/bin
```

To rerun some checks, name them. On Windows use `-Only python,python-bundle`. On Linux, pass the node path, the curl
directory or an empty string for each, and then the names of the checks.

The scripts test only through redirected profiles and disposable directories. They never change your real `PATH`,
registry or installed copy of wood.

## Assemble

```powershell
./tools/release/assemble.ps1 -WindowsBuild C:/release/windows -LinuxRelease LINUX/release `
    -LinuxBuildInfo LINUX/build-info.json -QualificationLinux LINUX/qualification-linux.txt -Output C:/release/final
```

This stops if the two platforms built different commits or source trees, and exits nonzero if any check's latest result
is not a pass. It writes the six release files, `SHA256SUMS`, `qualification.json` with every recorded attempt,
`SOURCE-COMMIT.txt` and `RELEASE_BODY.md`.

## Reproduce the source archive

```sh
git archive --format=tar --prefix=wood-SHORT/ COMMIT | gzip -n > wood-source-SHORT.tar.gz
```

`SHORT` is the first seven characters of `COMMIT`. The result should match the published hash when made with the same
gzip implementation.

## What is reproducible

| File | Repeated on one machine |
| --- | --- |
| Source archive | Byte-identical, and the same hash from Windows and Debian |
| Linux archive and setup | Byte-identical across two packaging runs from one payload |
| Linux release compiler, template and runtime object | Byte-identical across complete rebuilds, and from two different seeds |
| Windows archive and setup | Byte-identical across two packaging runs from one payload |
| Windows release compiler, template and launcher | Byte-identical across complete rebuilds |
| Windows runtime object `managed.o` | Differs in its two-byte COFF timestamp on each rebuild, so a complete Windows rebuild gives different archive and setup hashes |
| `.vsix` | Not reproducible. It is a zip with timestamps |

Other machines have not been compared. Different toolchains, Windows SDKs or gzip implementations may change the bytes, so
record the tools you used.

## After the build

Tag the release commit itself. The [publishing guide](publishing.md) has the steps.
