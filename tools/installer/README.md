# Building wood's installers

To build a whole release from a clean checkout, use [`tools/release`](../release) and
[releasing](../../docs/releasing.md). This page describes the installer pieces those scripts call.

Policy is wood code. `windows.wd` owns the seven-page native wizard;
`windows_services.wd` owns PATH/registration plans; the reusable
[`installer` package](../../packages/wood_installer/README.md) owns transactions.
`native/` supplies private Win32 controls, whitelisted registry operations and a
relocatable child-process launcher. No general GUI framework is added here.

The Windows **build machine** needs x64 Visual Studio/Windows SDK, C17, CMake,
Ninja, PowerShell 7, a native wood seed and its assets, and the pinned LLVM
tools in `llvm-tools.json`. Use a Visual Studio x64 developer shell. Dependencies
of the installed compiler are documented separately in
[the user guide](../../docs/windows-setup.md).

```powershell
cmake -G Ninja -S tools/installer/native -B build/setup-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/setup-native
wood lock tools/installer
wood build bootstrap/src/release_native_main.wd -O0 -o build/release-emitter.exe
```

The reviewed lock refresh is needed only when package bytes change. Do not
blindly replace a dependency lock obtained from another source.

Build new, absent output paths with `build-release-native.cmake`. Supply `EMITTER`,
`ASSETS` (a tested `share/wood`), `LLVM_DIRECTORY`, `SOURCE_ROOT`, `INPUT`, `OUTPUT`
and `OPTIMIZATION` (0 for the compiler, 2 for the small setup/launcher). It runs
the normal checked frontend/backend and remaps only the release application's
own diagnostic filenames to repository-relative names. Normal user compilation
retains its existing filenames and semantics. Supported targets are
`windows-x86_64` (default) and `linux-x86_64`; the latter requires `LINKER`.

For `windows.wd`, use `SUBSYSTEM=windows` and pass `LIBRARIES` as a semicolon-separated
list: `wood_installer_native.lib`, `wood_setup_windows.lib` and SDK import libraries
`user32.lib`, `gdi32.lib`, `advapi32.lib`, `comctl32.lib`, `ole32.lib`, `shell32.lib`,
`uuid.lib`. Embed `setup.manifest` using the SDK's `mt.exe -manifest` and
`-outputresource:OUTPUT;#1` before attaching any payload. For `launcher.wd`, link
`wood_setup_launcher.lib`; for `builder.wd`, link `wood_installer_native.lib`.
The compiler input is `bootstrap/src/driver_main.wd` without those private libraries.

When a release changes the native runtime, pass the freshly prepared object: `-RuntimeObject` for
`build-windows.ps1`, or a tenth argument for `build-linux.sh`. The current `runtime/native` sources are then
packaged with it, in place of the baseline archive's copies, so the compiler and its runtime always match.

`build-windows.ps1` accepts the baseline `CoreArchive`/`CoreSHA256`,
`Wood`, `LLVMDirectory`, compiled `Template`, `Builder`, `Launcher`,
`ReleaseCompiler`, and an absent `Work` directory. It verifies pinned inputs,
copies current reviewed Git source files without traversing junctions, records
source/payload manifests, generates the kernel32 import archive from ABI
declarations (not an SDK copy), and attaches the payload as `wood-setup.exe`.
It never overwrites an earlier build workspace or changes a protected installation.

```powershell
./tools/installer/qualify-windows.ps1 -Setup BUILD/wood-setup.exe `
  -Builder BUILD/installer-builder.exe -Template BUILD/setup-template.exe `
  -Payload BUILD/payload/wood-native -Evidence build/setup-ui-new
./tools/installer/qualify-portable.ps1 -Setup BUILD/wood-setup.exe `
  -EngineCLI BUILD/installer-cli.exe -ReferenceNative REFERENCE/bin/wood.exe `
  -Evidence build/setup-bootstrap-new
```

Replace `BUILD` and `REFERENCE` with your own paths. The UI test uses
only unique redirected registry scope and owned HWND/control IDs, never host
focus or injected keyboard/mouse input. No real user PATH/registration is changed.
The portable test replaces compiler stages **only inside its own
disposable installation**, preserving the stage-one copy and checking stages
two/three byte identity, reference IR, real mesh execution and relocation.

Linux retains `cli.wd`, not the Windows wizard. Run the package's CMake/CTest gate
with GCC/Clang and the Linux seed/reference; `SETUP_SANITIZERS=ON` instruments the
hosted C adapter test, but not the rest of the runtime. Optional generic shortcut metadata is retained for other
applications; this wood configuration exposes no shortcut option.

`build-linux.sh` consumes checksummed baseline/source archives, a native release
compiler/template/builder, a source epoch and an absent output directory. It uses
the existing native archive verifier and emits a standalone CLI setup.
`qualify-linux.sh` verifies both input hashes, installed workflows, native compiler
stages, reference IR, numerical mesh behavior, relocation and owned uninstall.
See each script's usage for its positional arguments. Linux LLVM tools remain
external; no system packages/configuration are installed or changed by these tools.
`archive-windows.ps1` similarly copies only hash-verified payload entries into new
staging and invokes the established reproducible native archive packager.
