# wood_audio 0.1.0

Audio for wood applications. Listed in the catalog as `wood@audio`.

```sh
wood add wood@audio
```

```wood
import audio.buffer as audio
import audio.wav as wav

chord = audio.mix(audio.tone(440.0, 0.5), audio.tone(660.0, 0.5))
wav.save("chord.wav", audio.normalize(chord, 0.8))
```

## Modules

| Module | Purpose | Platform |
| --- | --- | --- |
| `buffer` | Interleaved float buffers: tones, silence, gain, mix, fade, resample, mono mixdown | Pure wood, everywhere |
| `wav` | Read 8/16/32-bit PCM and 32-bit float WAV; write 8/16/32-bit PCM | Pure wood, everywhere |
| `device` | Blocking playback, device counts and explicit recording | Windows x86-64 with the adapter |

Decoders reject truncated or inconsistent files instead of trusting their headers.

## Device I/O

`device` needs the small C17 adapter in `native/`. It loads `winmm.dll` at use, so it
needs no Windows SDK import library. Build it once and link it explicitly:

```powershell
cmake -G Ninja -S native -B build/audio -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build/audio
wood build . -o player.exe --link-library build/audio/wood_audio.lib
```

Programs that import only `buffer` and `wav` need no adapter.

## Status and limits

Experimental. Exercised on one Windows 11 host: opening the output device and playing
a real clip to completion at volume 0. **Recording is implemented but has not been
exercised**; it only ever runs when an application calls `device.record`. Playback
blocks the caller, with no streaming, pause or device selection. There is no Linux
device backend, and MP3, FLAC and other compressed formats are not supported.
`resample` uses linear interpolation, which suits utilities rather than mastering.

Licensed MIT OR Apache-2.0.
