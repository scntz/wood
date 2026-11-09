# Windows setup

`wood-setup.exe` installs wood for your Windows user account on x86-64.

## Install

1. Check the download against `SHA256SUMS` from the release page:

   ```powershell
   Get-FileHash .\wood-setup.exe -Algorithm SHA256
   ```

   The setup is not code signed, so Windows may warn about an unknown publisher. A matching checksum shows the file
   wasn't damaged or changed after upload. It doesn't prove who made it.
2. Run it as your normal user. Accept the license (MIT OR Apache-2.0) and choose where to install. The default is
   `%LOCALAPPDATA%\Programs\wood`. For another folder, its parent must already exist, and the path must stay under 1,024 bytes.
3. **Add wood to my PATH** is on by default. It changes only your user environment. Setup creates no desktop shortcut,
   and there is no machine-wide install.
4. Open a new terminal and try it:

   ```text
   wood --version
   wood init hello
   wood run hello
   ```

The install includes the compiler, the three LLVM 19.1.7 tools it needs, the runtime, the source, examples and these
docs. Building programs needs no Visual Studio, Windows SDK, C++ compiler or Python.

Some things are not included:

- TLS and HTTPS need OpenSSL 3, and databases need SQLite 3.38 or newer. See
  [optional dependencies](installation.md#optional-dependencies).
- `--backend cpp` needs a source build. The installed compiler uses LLVM only.
- The GUI and audio packages need the Windows SDK to build their adapters, and are experimental, like Python interop.

## Upgrade and repair

Run the new setup and choose the same folder. Each version is kept in its own `versions/<hash>/` directory, so an upgrade
never overwrites the old files, and the previous version stays until you uninstall. PATH and the Installed apps entry move
to the new version only after its files are verified.

Running the same setup again repairs an install: missing files come back, and files you edited are left alone.

## Uninstall

Go to **Settings → Apps → Installed apps → wood → Uninstall**, confirm, and close the window when it finishes.

Uninstalling removes the files and settings setup created, and only wood's own PATH entry. Files you added or edited stay,
so the folder may not disappear completely. Terminals that were already open keep the old PATH until you reopen them.

The last step uses Windows PowerShell 5.1 to delete the uninstaller itself once it has exited. If a policy blocks PowerShell,
the install is left in place: run a downloaded setup with `--uninstall "C:\path\to\wood"` instead. Errors are written to
`uninstall-error-<id>.log` in the install folder.

## If something goes wrong

Setup checks the whole package before it touches the destination, and checks each file again as it copies. Cancelling
while it checks leaves nothing behind. Cancelling while it copies rolls back what it created. The final few steps that
register the install can't be cancelled.

If an install is interrupted, run the same setup again with the same folder, and it picks up where it stopped. An
interrupted uninstall can be resumed the same way, though files already deleted stay deleted.

If another copy of wood already owns the Installed apps entry, choose that copy's folder or uninstall it first. Don't
delete wood's registry values or receipt files by hand to get around this, and don't delete the whole folder to "repair" it.

## How it is tested

[`tools/installer/qualify-windows.ps1`](../tools/installer/qualify-windows.ps1) drives the real setup window: it installs,
builds and runs programs, repairs, upgrades and uninstalls. It runs setup with `--sandbox`, which redirects every registry
write to a throwaway key, and checks afterwards that your real PATH and Installed apps are unchanged. It runs on a
developer machine, not a fresh Windows install, and doesn't cover every screen scaling or accessibility setup.
