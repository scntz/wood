# wood_windowsgui 0.2.0

An **experimental Windows x86-64 GUI foundation**, with wood application code and
an optional C17 Win32 adapter. It uses native windows, STATIC/BUTTON/EDIT controls,
Segoe UI and system colors. It requires Windows 10 version 1703 or later APIs;
the exercised host is Windows 11. Linux/macOS, other architectures and older
Windows versions are unsupported. It is not a portable GUI framework.
Licensed MIT OR Apache-2.0, using the repository's license files.

## Build and use

From a matching Visual Studio x64 developer shell with Windows SDK and Clang:

```powershell
cmake -G Ninja -S packages/wood_windowsgui/native -B build/windowsgui `
  -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build/windowsgui
$sdk = Join-Path $env:WindowsSdkDir ('Lib/'+$env:WindowsSDKVersion.TrimEnd('\')+'/um/x64')
wood build examples/desktop_notes -o build/wood-notes.exe `
  --link-library build/windowsgui/wood_windowsgui.lib `
  --link-library "$sdk/User32.Lib" --link-library "$sdk/Gdi32.Lib" `
  --link-library "$sdk/Kernel32.Lib"
```

The explicit native compiler path can replace `clang`. A compatible COFF
librarian (`llvm-ar`, `llvm-lib` or Visual Studio `lib.exe`) must be available.
The adapter has no C/C++ runtime dependency. Native LLVM applications still need
the documented LLVM/linker toolchain; the reference backend needs C++23 normally.
The base wood installation does not link GUI libraries unless requested.
`native-inputs-Release.json` identifies the build output, target and import names;
it is not an integrity receipt. Record/reverify actual source, archive and SDK
hashes with [the explicit receipt procedure](../../docs/official-packages.md).

Declare a normal local dependency named `gui`, with path to this package and
version `0.2.0`, then `import gui.widgets as gui`. There are no automatic native
build hooks, downloads, registry entries or new manifest fields. Supply the
archive/import libraries to **every** build/run/test that uses GUI functions.
Source locks verify wood dependency bytes; they do not verify native archives.
The executable currently uses the console subsystem. The package does not
provide a windowed-subsystem build switch or installer.

## Lifecycle and threading

`run(title: str, width: int, height: int, context: str,
body: fn(int)->int)->int` creates a C-owned window scope and synchronously invokes
the raw wood function with a generation token. Construct `Window(token)` inside
that function. `window.context()` returns an owned byte-string copy of context;
JSON is useful for application settings. Return an integer application result.
The scope closes the window, controls, font and queued/context buffers before
propagating a callback error, cancellation or deferred `exit`. Callbacks must
return through wood's supported FFI guard; never invoke arbitrary native unwinding.
The token is an identity, not a pointer. Forging one does not grant raw memory access.

One lifecycle may be active per process. Run all GUI mutations and event waits on
its owning thread. Other threads can call `post` only. Use scoped tasks and join
them before returning from the body. Worker post lookup/copy/enqueue/wakeup is
protected against concurrent closure. Posting to a closed/stale token returns
false. Other stale-token operations raise R4010. A new window has a new token.
No wood function pointer/closure is retained asynchronously by Win32. Local
closures and owned records can be used in the ordinary wood event loop.

## Controls, events and limits

The [generated API](../../docs/official-packages-api.md) lists every public
signature. All text/control mutations return void or owned values and raise
R4010 with the operation and reason on invalid IDs, threading, encoding or limits;
OS failures include their Windows error number. The original wood callback error
takes precedence over the adapter's cancellation status. `task.checkpoint` raises
the established R4022. A pure compute loop must checkpoint explicitly.

- `Window.show()` shows without stealing focus; windows start hidden.
- `label`, `button`, `entry`, `textarea` create native controls. IDs are unique
  integers 1..65535; at most 256 controls. `textarea(...,readonly=true)` is useful
  for bounded history. Native Tab navigation is enabled.
- `set_text`/`text` write/read checked UTF-8. NUL and malformed UTF-8 are rejected
  before changing the control. Text is at most 1 MiB of UTF-8; interactive EDIT
  input is conservatively limited to 262144 UTF-16 code units. Context and posted
  payloads are byte strings and may contain NUL. They are each limited to 1 MiB.
- `listbox`, `list_add`, `list_clear`, `selected` and `select` manage a list; `selected`
  returns -1 with no selection. `checkbox`, `checked` and `set_checked` manage a check box.
- `image` creates a picture control. `set_image(control,pixels,width,height)` copies an
  RGBA `array[int]` of `width*height*4` values into it.
- `menu(name,control,text)` adds an item to a top-level menu, creating that menu on first use.
- `alert`, `error` and `confirm` show modal message boxes. `open_file` and `save_file` show
  the system file dialogs and return `""` on cancel. Filters look like `"Images|*.png;*.bmp"`.
- `move` changes a control's rectangle. Coordinates are nonnegative 96-DPI logical
  units, at most 32767; sizes are positive and at most 32767. Window client sizes
  are 96..32767. `Column(x,y,width,gap)` advances its own y coordinate when adding
  labels/buttons/entries; changes occur only after successful control creation.
- `wait()->Event` pumps OS messages and returns `kind`, `control`, `x`, `y`,
  `payload`. Kinds: `close`, `click`, `change`, `select`, `menu`, `resize`, `key`, `mouse`, `message`.
  `resize` carries logical client width/height; `key` carries the virtual-key
  number in x; `mouse` reports a left click on the window background with logical
  x/y. Child controls handle their normal input themselves. `message` carries
  the posting topic in control and an owned payload; old Event copies stay valid.
- `post(token,topic,payload="")->bool` is worker-safe; topic is nonnegative.
  The queue allows 1024 pending events and 4 MiB total payload including the
  currently returned event. Overflow raises an actionable error; there is no
  silent dropping or unbounded queue. System-event overflow becomes a checked
  wait error. Messages are FIFO by enqueue order, not reproducible scheduler order.

The message wait uses OS blocking with a 50 ms cancellation-check interval, not
a busy loop. Thread DPI context is restored on scope exit. Initial DPI scaling,
font updates and WM_DPICHANGED handling are implemented, but only a 96-DPI local
profile has been tested; moving between monitors with different DPI hasn't.
It uses standard controls, but screen readers and UI Automation have **not** been tested properly: the local .NET UIA
proxy test failed, while ordinary Win32 control tests passed. Don't assume it's accessible.

Dark-mode overrides, drawing surfaces, multi-window applications, retained closures, advanced layouts and other platform backends are deferred.
Do not call internal `_native` declarations as an application API.

## Executable validation

The [wood Notes application](../../examples/desktop_notes/README.md) demonstrates
the full lifecycle with SQLite and CLI parsing, and
[Media Desk](../../examples/media_desk/README.md) uses menus, dialogs, lists, check boxes
and images in one application. The [contract fixture](../../tests/ecosystem_gui/src/main.wd)
checks real controls, Unicode, errors, worker messages, queue backpressure and
repeated cleanup. Separate [exit](../../tests/ecosystem_gui/src/exit.wd) and
[cancellation](../../tests/ecosystem_gui/src/cancel.wd) fixtures exercise cleanup.
Use [the local GUI gate](../../tools/test-windowsgui-contracts.ps1) with explicit
library inputs, then [the OS interaction test](../../tools/test-windowsgui-ui.ps1)
on a freshly built Notes executable. These open only their own helper windows;
the interaction harness uses bounded native control messages without focus or
keyboard/mouse injection. See the saved ecosystem report for actual results.
