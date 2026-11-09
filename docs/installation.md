# Install wood

wood 1.1.0 runs on Windows and Linux, both x86-64. Download it from the
[release page](https://github.com/scntz/wood/releases/tag/v1.1.0).
This is the latest published research release. Check the [qualification scope](stability.md) for tested environments.

## Windows

Download `wood-setup.exe` and `SHA256SUMS`, and compare the hashes:

```powershell
Get-FileHash .\wood-setup.exe -Algorithm SHA256
```

Run the setup as your normal user, choose a location, keep **Add wood to my PATH** selected, and finish. Then open a
new terminal:

```text
wood --version
```

You should see `wood 1.1.0 (language 0.4.0, native runtime ABI 4)`.

The compiler, LLVM 19.1.7 tools and offline documentation are included. Ordinary builds need no Visual Studio, Windows
SDK, C++ compiler or Python. The installer is not code signed, so Windows may warn about an unknown publisher. A checksum shows the
download isn't damaged; it doesn't prove who published it. See [Windows setup](windows-setup.md) for upgrades,
repair and removal.

## Linux

Tested on Debian 13 x86-64 with glibc. Other distributions may work, but haven't been tested. wood uses your LLVM 19 tools
and GNU ld. On Debian 13:

```sh
sudo apt install llvm-19 binutils
export PATH="/usr/lib/llvm-19/bin:$PATH"
```

Add that `export` to your shell profile to keep it. Then download `wood-setup-linux-x86_64` and `SHA256SUMS`:

```sh
sha256sum -c SHA256SUMS --ignore-missing
chmod +x wood-setup-linux-x86_64
./wood-setup-linux-x86_64 install "$HOME/wood"
```

Setup prints the exact `export PATH=...` line. It edits no shell startup files, so add that line to your shell profile
yourself, open a new shell and run `wood --version`. To remove wood:

```sh
./wood-setup-linux-x86_64 uninstall "$HOME/wood" wood
```

Files you added or changed are kept, along with an empty `.setup-lock`. Delete the directory yourself if you no longer want them.

## Portable archives

`wood-core-windows-x86_64.tar.gz` and `wood-core-linux-x86_64.tar.gz` hold the same tree as the installers, under a
top-level `wood-native` directory, with `MANIFEST.sha256` listing every file. Extract one anywhere, keep the tree
intact, and run `wood-native/bin/wood`. It can be moved. Nothing is registered, so to remove it, delete the directory.
On Linux, check the contents with `sha256sum -c MANIFEST.sha256` from inside `wood-native`.

## Your first program

Create a file called `main.wd`:

```wood
print("hello, wood")
```

Then run it, or build an executable that runs without wood:

```sh
wood run main.wd
wood build main.wd -o hello
```

For packages and tests, create a project:

```sh
wood init hello-project
cd hello-project
wood run
wood test
```

Continue with the [tutorial](tutorial.md).

## Your editor

Download `wood-1.1.0.vsix` from the release page and install it:

```sh
code --install-extension wood-1.1.0.vsix
```

The extension is not on the Marketplace. Other editors can run `wood lsp` directly. See [editors](editors.md).

## Optional dependencies

Most programs need none of these. They are not bundled, and are found only when a program uses them.

| To use | You need |
| --- | --- |
| TLS and HTTPS | OpenSSL 3, and a trust store. On Debian: `libssl3t64` and `ca-certificates`. On Windows: `libssl-3-x64.dll` and `libcrypto-3-x64.dll` in System32 or in the absolute directory named by `WOOD_OPENSSL_DIR` |
| SQLite | SQLite 3.38 or newer. On Debian: `libsqlite3-0`. Windows uses a compatible system `winsqlite3.dll`, or the absolute directory named by `WOOD_SQLITE_DIR` |
| Packages | `wood add` needs `git` on your `PATH`, to fetch sources. Archive and Python sources also need `curl`, which Windows includes. See [how packages work](packages.md) |
| Python packages | `wood add python@name` provisions CPython 3.14 for the project. See [Python packages](python-distribution.md) |
| Desktop GUI and device audio | Windows only, with the Windows SDK to link their adapters. See [official packages](official-packages.md) |

TLS has been exercised on Debian with its OpenSSL 3, and on Windows with the OpenSSL 3 libraries that Git for Windows ships, named by
`WOOD_OPENSSL_DIR`. See the [support matrix](support.md).

## Build from source

A source build gives you the C++ reference compiler and test suites, and needs CMake 3.25 or newer and a C++23
compiler. The commands are in the [README](../README.md#build-from-source). To reproduce the release files themselves,
see [releasing](releasing.md).

## Troubleshooting

`wood check` reports errors without building anything, and `wood explain CODE` explains an error code offline. See
[troubleshooting](troubleshooting.md). In a bug report, include the exact command, your platform and the output of
`wood --version`, with private paths removed.
