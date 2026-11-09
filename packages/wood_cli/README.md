# wood_cli 0.1.0

Pure wood argument parsing and help formatting. No execution, downloads, global
state, terminal dependency or native adapter is involved. Licensed under the
repository's MIT OR Apache-2.0 terms.

Declare a local dependency alias in the application's `wood.json`, for example
`"cli":{"path":"../../packages/wood_cli","version":"0.1.0"}`, then import
`cli.args`. Review the package and run the existing `wood lock`, `deps --locked`,
`inspect --locked`, `test --locked` and `build --locked` workflows. These are local
exact-version dependencies, not registry coordinates or automatic installation.

## Public API

- `flag(name: str, short: str = "", description: str = "") -> Option`
- `value(name: str, short: str = "", repeat: bool = false, description: str = "") -> Option`
- `parse(arguments: list[str], options: list[Option]) -> Arguments`
- `help(program: str, options: list[Option]) -> str`
- `Option` fields: `name`, `short`, `description` (str), `takes_value`, `repeat` (bool).
- `Arguments` fields: `options: dict[str,list[str]]`, `positionals: list[str]`.
  Methods: `has(name)->bool`, `optional(name)->str|None`,
  `one(name,fallback="")->str`, `many(name)->list[str]`.

Names/short aliases are case-sensitive ASCII; long names start with a letter and
then accept letters/digits/hyphens. Short aliases are one letter. Duplicate
definitions fail before parsing. Parsing accepts `--key=value`, `--key value`,
boolean bundles (`-vq`), attached short values (`-ofile`), repeatable values and
`--` terminators. A value-taking option consumes its next argument even if it
starts with `-`. Negative numbers, a lone `-` and empty strings remain positionals.
Unknown options, missing values, duplicate nonrepeatable options and malformed
definitions raise R4010. There are no implicit abbreviations, environment reads,
response files, automatic type coercions or subcommand inference.

Limits: 256 option definitions, 4,096 arguments, 16 MiB total argument bytes.
Values and results retain normal COW/value semantics. `many` and `positionals`
can be copied and mutated independently. Inputs are already-tokenized byte strings;
the parser never reconstructs a shell command. It does not validate value UTF-8
or interpret option values as filenames. Help returns LF-separated text and does
not print or exit. User-provided help text is not terminal-escaped automatically.

Run the package's actual tests with `wood test packages/wood_cli --report=json`.
