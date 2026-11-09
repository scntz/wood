# Licensing and dependency inventory

Wood's own compiler, native primitives, standard library, tests, examples and
documentation are offered under **MIT OR Apache-2.0**. The original MIT grant is
preserved in LICENSE-MIT. LICENSE-APACHE contains the canonical Apache 2.0 text.
This choice does not change the licenses of external tools or libraries.

The frozen v0.4.0 release review found no vendored third-party source library.
The current LLVM development source additionally vendors six mathematical kernels
from musl 1.2.5 under `runtime/native/math/musl/`. Their Sun Microsystems
permission notices and upstream COPYRIGHT are preserved; see the
[provenance and adapter notes](runtime/native/math/README.md). Wood's dual license
does not replace those notices. No separately
bundled dependency binaries, downloaded SDKs, Python runtime, LLVM tool binaries,
OpenSSL or SQLite are included in the frozen source archive or historical Wood
ZIP packages. The new Windows setup development profile separately bundles the
three pinned LLVM 19.1.7 executables described below. Algorithm constants,
system ABI declarations and test protocols are not a bundled implementation of
those external projects. This is a repository/distribution inventory, not a legal
opinion or proof of every contributor's authorship.

| Component | Relationship | Terms / source |
| --- | --- | --- |
| musl 1.2.5 binary64 sin/cos and argument reduction | Six vendored source files, unchanged; private freestanding adapter | Sun Microsystems permissive notices in each file and [upstream COPYRIGHT](runtime/native/math/musl/COPYRIGHT); no linked musl/libm runtime |
| CMake / Ninja / Git | External development and packaging tools | [CMake BSD-3-Clause](https://cmake.org/licensing/), [Ninja Apache-2.0](https://github.com/ninja-build/ninja/blob/master/COPYING), [Git GPL-2.0](https://git-scm.com/about/free-and-open-source) |
| GCC / libstdc++ | External compiler; Linux packages use system runtime libraries | [GCC licensing and Runtime Library Exception](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html); system libraries are not redistributed |
| MSVC / Windows SDK | External Windows compiler/linker/import libraries | Governed by the installed Visual Studio/SDK license and [redistribution terms](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files); Windows release builds use the static Microsoft CRT, not a bundled redistributable installer |
| Clang / LLVM | External development compiler; Windows setup profile bundles unchanged `opt`, `llc`, `lld-link` 19.1.7 | [Apache-2.0 WITH LLVM-exception](https://llvm.org/docs/DeveloperPolicy.html#license), legacy and support notices under [third_party/llvm-19.1.7](third_party/llvm-19.1.7/README.md) |
| OpenSSL 3 | Optional dynamically loaded TLS runtime | [Apache-2.0](https://github.com/openssl/openssl/blob/openssl-3.5/LICENSE.txt); not bundled, configure a trusted installation |
| SQLite 3.38+ | Optional dynamically loaded database runtime | [Public-domain dedication](https://www.sqlite.org/copyright.html); not bundled; OS/vendor distributions may have their own packaging terms |
| CPython 3.14 | Optional dynamically loaded LLVM interoperability runtime; also an independent test oracle | [Python license](https://docs.python.org/3/license.html); operator-installed, not bundled or required by Core |
| NumPy | Optional interoperability example/test dependency | [NumPy licenses](https://numpy.org/doc/stable/license.html); operator-installed, not bundled; wheels can include separately licensed native dependencies |
| GitHub Actions | Remote CI tooling, not part of the compiler | Official checkout/upload actions are MIT, msvc-dev-cmd is MIT; exact revisions are pinned in workflows |
| actionlint | Optional maintainer workflow validator | [MIT](https://github.com/rhysd/actionlint/blob/main/LICENSE.txt); downloaded validation tool is not redistributed |

Applications built through the C++ backend may depend on platform runtime
libraries. Their terms and deployment requirements still apply. Wood's license
does not impose a license choice on independently written Wood programs. If a
future distribution bundles any external library or tool, inventory its exact
version, license, notices and redistribution requirements before shipping it.

## VS Code extension

The `.vsix` includes these npm packages with their own license files. None is part of the compiler or a compiled program.

| Package | License |
| --- | --- |
| `vscode-languageclient`, `vscode-languageserver-protocol`, `vscode-languageserver-types`, `vscode-jsonrpc` | MIT |
| `minimatch`, `@isaacs/nonrecursive-glob-match` | BlueOak-1.0.0 |
| `brace-expansion`, `balanced-match` | MIT |
| `semver` | ISC |

## Windows setup tool bundle

The setup profile includes only three unmodified tools from the official,
signature-verified `llvmorg-19.1.7` Windows archive. Exact archive/tool hashes are
pinned in [the installer tool inventory](tools/installer/llvm-tools.json).
The tools import Windows OS DLLs; no separately copied Microsoft runtime DLLs,
Windows SDK binaries/import archives, Clang compiler, CPython, OpenSSL or SQLite
are added to that profile. LLVM host tools are upstream C++ programs; their
implementation does not turn Wood's native applications into C++ programs.
The Core `kernel32.lib` is generated from Wood's ABI declarations, not copied
from the SDK. Normal native compilation uses the bundled tools and that archive.

LLVM licensing and support notices ship under `share/wood/licenses/llvm-19.1.7`.
The official build recipe enables rpmalloc and links libxml2 2.9.12 into its
support-library tool profile. Their notices are retained alongside LLVM's regex,
BLAKE3, xxHash, MD5 and SHA-1 notices. No separately linked zlib/zstd/libffi/libedit
runtime is asserted; the inspected release configuration disables those features.
The repository stores those notices/provenance, not LLVM implementation code or
binary executables. Windows PowerShell is an OS dependency used only to wait for
the Wood uninstaller and remove its now-unmapped, hash-verified executable.

## Public TLS fixtures

`tests/fixtures/tls` contains deliberately public, offline CA/certificate/private
key identities for correct/expired/wrong-authority tests. They are not credentials
for a service. The release audit pins the three key files by SHA-256 and rejects
any other private key. Never deploy these identities or treat them as secrets.

## Audit boundary

The release report records actual source/archive scans and binary dependency
inspection. No automatic scan can establish absence of all secrets, provenance
violations or vulnerabilities. Ignored local build trees, tool caches and backup
archives remain private and are not part of the Git tree or source release.
