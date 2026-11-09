# Media Desk

A native Windows desktop library for images and audio, written in wood. It is here to prove the
application packages work together in one real program, not to be a finished media player.

- Scan a folder on a background task, or add single files from the file dialog. PNG, BMP and
  WAV files are kept in a SQLite library.
- Preview images in a native picture control.
- Play WAV files through the audio device, on a background task.
- Remember the folder if asked, read settings from a config file, and write a log.

It uses `wood_windowsgui`, `wood_image`, `wood_audio`, `wood_config`, `wood_log`, `std.sqlite`,
`std.fs`, `std.path` and `std.task`. SQLite is loaded at run time and is not bundled.

## Build

From a Visual Studio x64 developer shell, build the two native adapters as the
[GUI](../../packages/wood_windowsgui/README.md) and [audio](../../packages/wood_audio/README.md)
packages describe, then:

```powershell
$sdk = Join-Path $env:WindowsSdkDir ('Lib/'+$env:WindowsSDKVersion.TrimEnd('\\')+'/um/x64')
wood build examples/media_desk -o media-desk.exe `
  --link-library wood_windowsgui.lib --link-library wood_audio.lib `
  --link-library "$sdk/User32.Lib" --link-library "$sdk/Gdi32.Lib" `
  --link-library "$sdk/Kernel32.Lib"
```

## Run

```powershell
media-desk.exe --make-samples samples
media-desk.exe --database library.db --log desk.log --volume 0.5
```

`--make-samples` writes images and a WAV to try it with.

## Tests

[`tools/test-media-desk.ps1`](../../tools/test-media-desk.ps1) starts the built application outside
the source tree and drives it through its own window handles and control IDs, with no keyboard or
mouse injection, playing audio at volume zero. Its checks include scanning, previews with their
displayed pixels, playback, persistence across a restart, and the log.

Limits: Windows only, WAV only for audio, no recording, and no accessibility qualification.
