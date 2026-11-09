# Distributing wood and wood programs

## Shipping your own programs

Programs you write in wood can use any license you like. A built program contains wood's runtime, so when you distribute it,
include wood's copyright notice and the MIT or Apache-2.0 license text, along with the notices of any other library you link.
The license files in your wood installation don't cover your own distribution. See the
[third-party notices](../THIRD_PARTY_NOTICES.md).

To ship an installer for your program, use the [`wood_installer` package](../packages/wood_installer/README.md).

## Packaging a source build

The installers and archives on the release page are built by [`tools/release`](releasing.md). This section is for packaging
the C++ reference compiler from a source build, which needs a C++23 compiler at run time to build programs.

Build and test first, then package with the same configuration:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release
```

For a checked archive, install to a new prefix and use the packager, which refuses to overwrite its output:

```sh
cmake -DPREFIX=/absolute/installed-wood \
  -DOUTPUT=/absolute/releases/wood-linux-x86_64.zip \
  -P tools/package-release.cmake
```

The archive has one top-level directory with `bin/wood` (`wood.exe` on Windows) and `share/doc/wood/`, and a
`MANIFEST.sha256`. The packager extracts it again, checks every file against the manifest, runs the extracted compiler with
no C++ compiler available to check that source checking still works, and writes a SHA-256 file beside it.
`tools/package-selfhost.cmake` does the same for the [`wood-selfhost`](selfhost-driver.md) profile.
`tools/verify-install.cmake` goes further: it builds real programs and locked projects with the installed compiler.

It includes the compiler, runtime, standard library sources, examples, docs, licenses and the source and tests, but no
Python, SQLite or OpenSSL. Extracting it changes nothing else on the system.

For public builds:

- Configure with `-DWOOD_RELEASE_BUILD=ON`, which replaces build paths in the compiler's own file names with `wood-source/`.
- On Windows, also use `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`.
- Before packaging the Windows bridge library, strip debug records from private copies of its three objects with
  `llvm-objcopy --strip-debug`, then rebuild the archive with `tools/release-msvc-archive.cmake`. Release objects can still
  contain absolute build paths otherwise.

## Checking a package

Test the archive, not the directory you built it from:

1. Extract it into a new path, ideally one with spaces and non-ASCII characters.
2. Check `wood --version` and `wood --language-info`.
3. Run `wood test share/doc/wood/examples/archive --locked --report=json`.
4. Build and run `examples/tree_report.wd` from the installed copy.
5. Build an executable and run it with the compiler and source moved out of the way.

Neither CPack nor these scripts sign anything. A checksum shows the file is intact; it doesn't prove who built it.
