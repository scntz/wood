# Tools

Scripts for building, packaging and checking wood. Most are CMake scripts run with `cmake -P`, and each states
the variables it needs.

| Path | Purpose |
| --- | --- |
| `release/` | Build, qualify and assemble a whole release from a clean checkout. See [releasing](../docs/releasing.md) |
| `installer/` | The setup wizard, Linux installer and their packaging. See [its guide](installer/README.md) |
| `build-native.cmake`, `prepare-native-runtime.cmake`, `prepare-native-assets.cmake` | Build the native compiler, runtime object and assets |
| `install-native.cmake`, `package-native.cmake`, `package-selfhost.cmake`, `package-release.cmake`, `release-msvc-archive.cmake` | Install and package each compiler profile |
| `native-package-receipt.cmake`, `verify-install.cmake`, `qualify-native-consumer.cmake`, `qualify-native-preview.cmake` | Check an installed or packaged toolchain |
| `selfhost_seed.cmake`, `embed-text.cmake`, `release-source-paths.cmake` | Helpers used by the build |
| `vendor-library.cmake` | Export a library from a Git revision into a project's `vendor/` |
| `check-docs.cmake`, `docs-api.cmake`, `check-official-packages.cmake`, `check-native-coverage.cmake` | Documentation and coverage gates |
| `release-audit.ps1` | Audit public files and history for private data |
| `validate-linux.sh` | The Linux test gate. See [linux](../docs/linux.md) |
| `bench-dx.ps1` | Measure compiler and tool timings. See [performance](../docs/performance.md) |
| `test-media-desk.ps1`, `test-python-bundle.ps1`, `test-windowsgui-contracts.ps1`, `test-windowsgui-ui.ps1` | Windows application and bundle checks |
