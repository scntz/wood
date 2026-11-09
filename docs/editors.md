# Editors

`wood lsp` is a language server. It speaks the Language Server Protocol on standard input and
output, so any editor with an LSP client can use it. It belongs to the native CLI, and the C++
reference driver does not provide it.

## Visual Studio Code

The extension lives in [`editors/vscode`](../editors/vscode/README.md). It is experimental and not on the
Marketplace, so download `wood-1.1.0.vsix` from the
[release page](https://github.com/scntz/wood/releases/tag/v1.1.0) and install the file:

```bash
code --install-extension wood-1.1.0.vsix
```

To build it from source instead:

```bash
cd editors/vscode && npm ci --omit=dev && node scripts/package.mjs ../../build/vsix
```

It finds `wood` on `PATH` and in the default install locations. To use another copy, set `wood.path`
in your user settings.

## Other editors

Start the server with `wood lsp`, for files with the `.wd` extension and language ID `wood`. A project
root is a directory containing `wood.json`. For example, in Neovim:

```lua
vim.lsp.start({ name = "wood", cmd = { "wood", "lsp" },
  root_dir = vim.fs.root(0, { "wood.json" }) })
```

Only VS Code has been tested. The protocol tests in `tests/lsp` run against the server directly.

## What works

| Feature | Notes |
| --- | --- |
| Diagnostics | Syntax, types, undefined names and imports, with the compiler's codes. Every failing statement in a file is reported, and errors caused by an earlier one are suppressed |
| Project-wide checking | Files under a `wood.json` are checked in the background, even when closed. Lone scripts are checked when opened |
| Unsaved edits | Seen across files, and a document does not need to exist on disk |
| Hover | Signatures, types and doc comments. A doc comment is the `#` lines directly above a declaration, or at the top of its body |
| Go to definition | Crosses files, packages and the standard library. Includes record fields, methods, keyword arguments and import lines |
| Find references | Resolved through the compiler, so same-named symbols are told apart |
| Outline and workspace symbols | Functions, records, fields, methods and top-level variables. Workspace search is fuzzy |
| Completion | Names in scope, module members, record fields and methods, standard modules after `import`. Works on a line that does not parse yet |
| Signature help | User functions, methods, record constructors and documented builtins |
| Rename | Locals, parameters, functions, records, fields and methods. See below |
| Semantic tokens | Modules, types, functions, methods, parameters, variables and fields |
| Sync | Incremental, in UTF-16 positions as the protocol requires |

## How it keeps up with typing

Analysis waits until input has been quiet for 120 ms, and a request that needs an answer forces it.
Only the file being edited has its function bodies checked. Other modules contribute their
declarations, and a parse is reused while a file's text is unchanged. Background checking does one
file at a time and yields to any waiting message.

When a line does not parse, the same parser runs again with that line replaced, so line numbers stay
put and the rest of the file stays useful. This is recovery by retry, and a badly broken file costs
more than a clean one. Names the broken line would have declared do not produce follow-on errors.

## Rename safety

Rename refuses when it cannot be sure:

- when any use of that name in the workspace could not be resolved, since it might be this symbol;
- when a file involved has errors the analyzer cannot get past;
- when the new name is not a valid name, or would cause a new error. The edit is tried in memory first,
  so a collision is caught;
- for builtins, modules, the standard library and installed packages under `vendor`.

## Known limits

- Hover and go to definition work on names. Hovering an arbitrary expression, such as an operator, shows
  nothing, even when the compiler knows its type.
- A broken declaration header hides that declaration until it parses. Uses of it are not reported as
  undefined in the meantime.
- An error in a signature, or a duplicate declaration, stops semantic analysis of that file. It is
  reported, and completion keeps using the last good analysis, while hover and navigation return nothing
  until it is fixed.
- A generic function's body is only checked where it is called, as in the compiler. So an unused generic
  function gets no diagnostics for its body.
- `from module import` does not complete names yet, only module names.
- Completion after a call chain needs a known type, so a chain through a polymorphic builtin offers nothing.
- There is no code action, inlay hint or call hierarchy support. Formatting is the whole document,
  as `wood fmt` lays it out; there is no range formatting.
- `$/cancelRequest` is ignored. Requests run in order and most finish quickly.
- A background check uses a single 2048-file limit per session, and skips hidden directories, `build*`,
  `node_modules`, `dist`, `install`, `out` and `vendor`.
- Python packages show the bridge's `call` and `attr`, not the Python library's own API.
