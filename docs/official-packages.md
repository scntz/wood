# Official packages

The official packages live in [`packages/`](../packages/catalog.json). They use the same manifests and commands as any
project, are listed in the [catalog](packages.md) as `wood@name`, and install with `wood add`. They're at 0.x versions,
and all but `wood_cli` are experimental, so expect changes. To write your own, see [publishing a package](package-publishing.md)
and the [library walkthrough](libraries.md).

| Package | What it does | Notes |
| --- | --- | --- |
| [wood_cli](../packages/wood_cli/README.md) | Argument parsing and help text | Pure wood, Windows and Linux |
| [wood_windowsgui](../packages/wood_windowsgui/README.md) | Native desktop windows, controls and events | Experimental. Windows only, with a C17 adapter and Windows SDK import libraries |
| [wood_audio](../packages/wood_audio/README.md) | Audio buffers, WAV files and device playback | Experimental. Pure wood, apart from the Windows playback adapter |
| [wood_image](../packages/wood_image/README.md) | PNG and BMP, resizing, 2D drawing and 3D mesh rendering | Experimental. Pure wood |
| [wood_log](../packages/wood_log/README.md) | Logging with levels | Experimental. Pure wood |
| [wood_config](../packages/wood_config/README.md) | INI-style configuration with environment overrides | Experimental. Pure wood |
| [wood_process](../packages/wood_process/README.md) | Running programs, and finding files next to the executable | Experimental. Pure wood, LLVM backend only |

Their APIs are listed in the [package API](official-packages-api.md). For HTTP, TLS, SQLite, geometry and numerical work,
use the [standard library](stdlib.md) directly.

## Without network access

`wood add wood@image` fetches from the wood repository on GitHub. Every installation also has these packages in
`share/wood/packages`, so without network access you can copy one into your project and declare it as a path dependency:

```json
{"format":1,"name":"app","version":"0.1.0","source":"src","entry":"main.wd",
 "dependencies":{"image":{"path":"vendor/wood_image","version":"0.1.0"}}}
```

Then run `wood lock`. `wood_windowsgui` and `wood_audio` also need their native adapters, as their READMEs explain.

## Vendoring and updates

An application can keep a reviewed copy in `vendor/wood_cli` and give it a name of its choosing:

```json
{"format":1,"name":"example","version":"0.1.0","source":"src","entry":"main.wd",
 "dependencies":{"cli":{"path":"../vendor/wood_cli","version":"0.1.0"}}}
```

The code then uses `import cli.args as cli`. Names are ASCII identifiers. Versions are exact `x.y.z` and must match; there
are no ranges.

```sh
wood check application
wood lock application
wood deps application --locked
wood inspect application --locked
wood test application --locked --report=json
wood build application --locked -o application-bin
```

Run `lock` only after reviewing a dependency change, keep the manifest, sources and lock in version control, and build with
`--locked`. Then a changed byte in a dependency fails the build instead of quietly updating the lock. A lock records what
you reviewed; it isn't a signature or an audit of native code.

`wood test packages/wood_cli` runs a package's own tests. The Notes model under `examples/` is sample code, not a public package.

## Native libraries

The GUI package needs its C17 adapter built and passed with `--link-library`, together with the Windows SDK's `user32`,
`gdi32` and `kernel32` import libraries. The paths must name existing `.a` or `.lib` files: a package can't make the
compiler search for system libraries. The SDK's license is yours to follow, so don't commit SDK libraries to your repository.

To record exactly which native inputs you used, after building the adapter:

```powershell
$imports = "$sdk/User32.Lib;$sdk/Gdi32.Lib;$sdk/Kernel32.Lib"
cmake -DPACKAGE_DIR=packages/wood_windowsgui `
  -DARCHIVE=build/windowsgui/wood_windowsgui.lib "-DIMPORT_LIBRARIES=$imports" `
  -DCOMPILER=clang -DOUTPUT=build/gui-inputs.json -P tools/native-package-receipt.cmake
cmake -DPACKAGE_DIR=packages/wood_windowsgui `
  -DARCHIVE=build/windowsgui/wood_windowsgui.lib "-DIMPORT_LIBRARIES=$imports" `
  -DVERIFY=build/gui-inputs.json -P tools/native-package-receipt.cmake
```

The receipt records the hashes and sizes of the adapter, its source and the SDK libraries, plus the ABI, target and compiler
version, but no machine paths. Verifying rejects any changed byte. It never builds, downloads or runs anything, and it
doesn't prove the compiler is genuine or that the native code is reproducible.

## Maintaining a package

An official package needs a versioned manifest, a list of public modules, documentation of its platforms, errors and limits,
and real tests. Update the catalog, then regenerate and check the API page with
`cmake -DUPDATE_API=ON -P tools/check-official-packages.cmake`. `tests/official_packages.cmake` tests every package with the
installed compiler; the GUI package has its own Windows test.
