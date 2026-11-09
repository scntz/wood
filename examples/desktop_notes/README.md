# wood Notes

A real Windows desktop application: native labels, button, text input and a
resizable read-only history view, backed by prepared SQLite operations in the
shared `notes_store` example package. All application/model code is wood. The
optional `wood_windowsgui` package supplies a C17 Win32 adapter.

Build with the matching current LLVM-native compiler, the adapter archive and the
Windows SDK user32/gdi32 import archives. These are explicit `--link-library`
inputs, not package hooks. Run the executable with `--database /path/notes.db`;
`--help` describes the argument parser. The default database is `wood-notes.db`
in the current directory. Opening creates the table if needed; saving appends a
note and does not replace an existing database. Empty/NUL/invalid-UTF-8/over-64-KiB
notes are rejected. Recent history queries at most 20 notes and displays at most
512 KiB plus a short omission notice. Larger valid databases remain usable;
omitted display content is preserved in SQLite. SQLite supplies the
stored UTC timestamp; this does not add a wood wall-clock module.

The window is shown without stealing focus. Click it normally to interact. Use
Tab to navigate native controls, type a note and press Save note. Closing the
window releases GUI and database resources. This is a Windows foundation, not
a portable GUI framework: Linux/macOS, dark-mode overrides, menus/dialogs,
advanced accessibility and high-DPI monitor-transition qualification remain
outside the initial verified subset. Consult the package's actual qualification.
