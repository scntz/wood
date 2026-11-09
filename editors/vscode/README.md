# wood for Visual Studio Code

Language support for [wood](https://github.com/scntz/wood), a native programming language built for
humans and designed for AI.

**Preview.** This needs wood 0.5 or newer, which provides the `wood lsp` command. It is not on the
Marketplace yet, so install it from a `.vsix` file.

## Features

- **Diagnostics in the Problems panel**, as you type, for every error in the file, with the compiler's
  own codes and messages. Project files are checked even when they are not open.
- **IntelliSense**: names in scope, module members and record fields and methods, including on a line
  that does not parse yet.
- **Hover** with signatures, types and doc comments, and **signature help** as you type arguments.
- **Navigation**: go to definition, find references, the outline, and workspace symbol search.
  Go to definition reaches the standard library.
- **Rename**, which refuses rather than guesses when an edit would not be safe.
- **Format Document**, in the same layout as `wood fmt`. This needs wood 1.1.0 or newer.
- **Semantic highlighting**, on top of a grammar that works without the server.
- **Run, build, check and test** from the Command Palette or as tasks, with errors linked to the source.
- Snippets, and validation for `wood.json`.

## Install

```bash
code --install-extension wood-1.1.0.vsix
```

The extension finds `wood` on `PATH` and in the default install locations. To use another copy, set
`wood.path` in your user settings. It cannot be set by a workspace, because a project must not be able
to choose a program for you to run. For the same reason the extension does not run in untrusted
workspaces.

## Settings

| Setting | Purpose |
| --- | --- |
| `wood.path` | The `wood` executable. User settings only |
| `wood.languageServer.enabled` | Run the language server |
| `wood.trace.server` | Log protocol messages to the output panel |

## Tasks

The `wood` task type takes a `command` of `run`, `build`, `check` or `test`, plus optional `file` and
`args`. Errors are matched by the `$wood` problem matcher.

```json
{
  "version": "2.0.0",
  "tasks": [
    { "type": "wood", "command": "build", "args": ["-o", "app"], "problemMatcher": ["$wood"], "group": "build", "label": "wood: build" }
  ]
}
```

## Platforms

Tested on Windows 11. The extension has no platform-specific code beyond finding the executable, and
the language server's protocol tests also pass on Debian, but the extension itself has not been run
inside VS Code on Linux.

## Limits

See [Editors](https://github.com/scntz/wood/blob/main/docs/editors.md) for what the language server
does not do.
