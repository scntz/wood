# Working on wood

These are the rules for anyone, person or agent, changing this repository. To write programs in wood rather than change
wood itself, read [docs/agents.md](docs/agents.md).

## Before you start

- Read [docs/language.md](docs/language.md) before writing wood code or changing how the language behaves.
- Read [docs/architecture.md](docs/architecture.md) and [docs/contributors.md](docs/contributors.md) before changing the
  compiler, runtime or standard library.
- Read [docs/native-computing.md](docs/native-computing.md) before touching arrays, views, borrowing, numerical kernels or
  numeric-buffer FFI.

## The language

- The distribution is 1.1.0. The language is 0.4.0 and the native runtime ABI is 4. Don't describe roadmap items as working.
- Keep wood's own syntax: `fn` for functions, and indentation with `:` for blocks. Python is a reference for simplicity,
  not for grammar or semantics.
- wood never needs Python. Python interop is optional and experimental, and normal compilation must work without it.
- Prefer wood for library code. Use C or C++ only for core primitives and OS integration. Add no third-party libraries.
- Keep value semantics, copy on write, checked integers, checked indexing and shape arithmetic, and the documented
  floating-point behavior.
- No new syntax without a design discussed with the owner first.

## The compiler

- Keep the lexer, parser, semantic analysis, backends and driver separate.
- A public change updates [docs/language.md](docs/language.md), [docs/grammar.ebnf](docs/grammar.ebnf),
  [docs/language.json](docs/language.json) and the other machine-readable metadata.
- Test what must fail as well as what must work, and run native programs rather than only checking they compile.
- A compiler change must still bootstrap: stage 2 and stage 3 must match byte for byte. See
  [docs/native-backend.md](docs/native-backend.md).
- Unsupported features on the LLVM backend raise B3501. Never fall back to C++ silently.

## Testing and claims

- Build with CMake and test with CTest, as [docs/contributors.md](docs/contributors.md#build-and-test) describes. MSVC native
  tests need a Visual Studio x64 developer shell.
- Validate on both Windows and Linux.
- A configured, skipped, interrupted or timed-out check is not a pass.
- No performance claim without a measurement someone can reproduce and a stated baseline. Don't benchmark on a busy machine.

## Installer

The installer engine stays separate from the Windows wizard and from each application's settings. wood's own installer offers
an install location and an Add to PATH option that is on by default, and no desktop shortcut. Tests must redirect system
integration and never change the real PATH, registry, installed programs or app registrations.

## Git

- Work goes to `main`. Commit and push finished, tested work there with a normal fast-forward push. The owner has given
  standing permission for this.
- The workflows run on every push to `main` and on pull requests.
- Never force-push, rewrite history or push with `--mirror` or `--all`.
- Tags, releases, repository visibility, security settings and publishing the VS Code extension each need the owner's
  explicit approval. Published releases are never replaced.
- Never commit credentials, local build output or large caches.

[CLAUDE.md](CLAUDE.md) repeats these rules for Claude Code. If the two disagree, this file wins.
