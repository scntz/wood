# Reusable native installer engine

This package is the engine behind [wood's Windows setup](../../docs/windows-setup.md) and its Linux installer, and you
can use it for your own programs. On its own it doesn't produce an installer: you pair it with a frontend and a payload.

The engine, manifest validation, payload builder, transaction policy, recovery
and CLI frontend are written in wood. A private C17 adapter supplies streaming
SHA-256 and rooted filesystem operations on Windows/Linux x86-64. The existing
compiler, runtime, public standard library and ABI 4 are unchanged.

## Implemented behavior

- Attach a payload and canonical JSON manifest to a compiled native template.
  The builder uses exclusive output creation and verifies its completed output.
- Validate template/manifest/file SHA-256, exact contiguous extents, architecture,
  portable relative names, duplicate ASCII-case names, file/directory collisions
  and fixed size/count limits. Links and special source files are rejected.
- Verify the entire payload before creating a destination, and verify each file
  again during extraction. SHA-256 detects corruption; it is not publisher
  authentication. Verify downloaded artifacts against a trusted checksum.
- Install into an empty directory or a matching managed destination. New bytes
  occupy `versions/<manifest-sha256>/`; unknown existing files are not overwritten.
- Repair missing files, refuse modified files, retain prior generations during
  upgrades, and atomically activate a receipt only after file deployment.
- Journal intended creations before mutation. Roll back cancellation/errors and
  recover an interrupted deployment on the next operation. Recovery preserves
  changed/unknown data and retains its journal for review when cleanup is unsafe.
- Uninstall recorded unchanged files across retained generations. Preserve edits
  and additions; never recursively delete an installation directory. A closed,
  persistent `.setup-lock` file remains so another process cannot accidentally
  acquire a different lock identity during cleanup.

These are synchronous, sequential operations. Do not call the private adapter
from tasks or use it as a public filesystem API. Concurrent, uncoordinated edits
to installer metadata are unsupported. This is not an OS security sandbox.

## Build and exercise

You need an installed `wood`, CMake, Ninja and a C17
compiler. On Windows use an x64 Visual Studio developer shell and its Windows
SDK. The following are build-machine dependencies; the resulting native CLI
installer itself does not invoke a compiler or require wood/Python to be installed.

```powershell
cmake -G Ninja -S packages/wood_installer/native -B build/installer-adapter `
  -DCMAKE_C_COMPILER=cl -DCMAKE_BUILD_TYPE=Release -DWOOD=C:/tools/wood/bin/wood.exe
cmake --build build/installer-adapter
ctest --test-dir build/installer-adapter --output-on-failure
wood build tools/installer/cli.wd --link-library build/installer-adapter/wood_installer_native.lib -o build/installer-cli.exe
wood build tools/installer/builder.wd --link-library build/installer-adapter/wood_installer_native.lib -o build/installer-builder.exe
```

Replace the example compiler path with the actual installation. A local package
dependency in [the tool manifest](../../tools/installer/wood.json) resolves the
`installer` alias. There is no `wood@installer` syntax or `wood installer build`
command. No package hook, network download or registry has been introduced.

The test script builds a real sample application and runs the equivalent of these commands in throwaway directories:

```text
installer-builder TEMPLATE CONFIG.json PAYLOAD-DIRECTORY OUTPUT
OUTPUT install DESTINATION
OUTPUT uninstall DESTINATION APPLICATION-ID
```

The Linux CMake configuration uses `-DCMAKE_C_COMPILER=gcc` or `clang-19`, a Linux
native wood path and `libwood_installer_native.a`. Generated Linux bundles are
marked executable by the builder. Linux executable discovery currently uses
`/proc/self/exe`, so it needs procfs.

## Application metadata and wood's presentation policy

The [wood configuration](../../tools/installer/config/wood.json) describes a
command-line language: `desktop` is empty, `path_default` is true, and the optional
Start Menu target is offline documentation. The tests check exactly this
configuration. The reusable engine does not hardcode wood's identity.

The [sample configuration](../../tools/installer/config/sample.json) retains
optional desktop/Start Menu target metadata for applications that need it.
Targets must be payload files. **These fields are validated presentation metadata;
the generic CLI does not publish PATH, shortcuts or Installed Apps integration.**
The separate Windows wood frontend supplies owned PATH/Installed Apps plans, but
exposes no shortcut option. Creating shortcuts isn't tested.
No automated test changes the developer's real PATH or registration.

Accepted configuration keys are `format`, `id`, `name`, `version`, `publisher`,
`architecture`, `license`, `command`, `path_default`, `desktop`, `start_menu`.
Unsupported keys fail rather than pretending to implement components or hooks.
The builder generates `files`, `template_size` and `template_sha256`.

## Bounds, errors and ownership

- 16,384 payload files; 32,768 traversed input entries; 1 GiB per file; 2 GiB
  total payload; 4 MiB canonical manifest/journal/receipt; up to 32 generations.
- Names contain 1..1,024 UTF-8 bytes, components at most 200 bytes. Reject empty,
  dot/traversal components, control/NUL bytes, Windows device names, forbidden
  punctuation, trailing dot/space and ASCII-case collisions. Names must also be
  accepted by the destination filesystem; full Unicode case equivalence is not
  implemented by the manifest validator.
- Windows holds ancestor directory handles without delete sharing and rejects
  reparse points. Linux uses no-follow directory-relative operations and a root
  lock. Windows destinations must be absolute drive paths; UNC roots are deferred.
- The adapter uses fixed buffers and closes acquired file/directory handles on
  its return paths. wood closes the root on checked failures. Process-crash
  recovery is tested; power loss and hostile filesystem races are not.
- Windows publication retries only access/sharing/lock statuses using five sleeps
  totalling 310 ms; scheduling can extend wall time. It then reports the original
  error and publication stage. It never falls back to copying over the target.
- Existing wood structured runtime errors remain intact. Installer failures raise
  checked errors with operation context. CLI usage errors exit 2; uncaught checked
  runtime failures use wood's existing exit status 70.

## Frontend integration and boundaries

`install` accepts an optional list containing one frontend-owned
`callable(str,str,json) -> json`. It prepares an integration plan, journals it
before publication, and invokes apply/rollback alongside the existing receipt
commit. `uninstall` similarly journals its settings plan and accepts an exact
deferred-file list for a mapped Windows uninstaller. `installed_receipt` validates
and locks a receipt for inspection. Existing calls with no hooks remain valid;
the current generic CLI rejects a receipt requiring its original frontend.
These are trusted frontend callbacks, not hooks supplied by payload metadata.
Do not use historical prototype CLIs to uninstall a GUI-managed root.

The Windows wizard has seven pages: the license, a folder browser, consent, progress and errors, registration for the
current user, and cleanup after exit that checks hashes. The [installer build guide](../../tools/installer/README.md) covers
building and testing it. Machine-wide installs, signing, other architectures, macOS and arbitrary hooks aren't supported.
Recovering from a crashed process doesn't mean surviving a power cut.

Licensed MIT OR Apache-2.0; the package includes the actual repository license texts.
