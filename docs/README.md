# wood documentation

New to wood? Start with [installation](installation.md) and the [tutorial](tutorial.md). The links here are relative, so they
work offline, in a checkout or an installed copy.

These guides describe the wood 1.1.0 research release. Wood has no scheduled releases or ongoing
support commitment. The [qualification summary](stability.md) records what was tested and what remains unverified.

## Getting started

- [Installation](installation.md) and [Windows setup](windows-setup.md)
- [What's new in 1.1.0](releases/1.1.0.md)
- [Tutorial](tutorial.md)
- [Examples](examples.md)
- [Editors and the language server](editors.md)
- [Troubleshooting](troubleshooting.md) and [error codes](reference/explanations.md)
- [Browser playground](playground.md)
- [Known limitations](known-limitations.md), [stability and compatibility](stability.md) and the [support matrix](support.md)

## The language

- [Language reference](language.md) and [grammar](grammar.ebnf)
- [Types and calls](types-and-calls.md), including tuples, optionals and generics, and [return inference](return-inference.md)
- [Generic records](generic-records.md)
- [Closures and iterators](closures-iterators.md)
- [Data model](data-model.md): dictionaries, records and JSON
- [Errors](errors.md) and [resource scopes](language.md#scoped-native-resources)
- [Coming from Python](python-compatibility.md)

## Standard library

- [Library guide](stdlib.md), [API index](stdlib-api.md) and [module overview](stdlib-modules.md)
- [Networking](networking.md): streams, TCP and HTTP
- [Backend services](backend.md): SQLite, TLS, HTTPS and tasks
- [Native computing](native-computing.md): arrays and numerical kernels, and [OBJ meshes](obj.md)
- [Calling C](native-ffi.md)

## Projects and packages

- [Projects](projects.md): modules, local packages and `wood.lock`
- [How packages work](packages.md), [Python packages](python-distribution.md) and [publishing a package](package-publishing.md)
- [Official packages](official-packages.md) and their [API](official-packages-api.md)
- [Vendoring a library by hand](libraries.md)
- [Command line](cli.md) and the [native toolchain](native-cli.md)
- [Guide for coding agents](agents.md) and [language metadata](language.json)

## Working on wood

- [Contributing](../CONTRIBUTING.md) and [testing](contributors.md)
- [Architecture](architecture.md), the [compiler written in wood](bootstrap-compiler.md), the
  [`wood-selfhost` driver](selfhost-driver.md) and the [LLVM backend](native-backend.md)
- [Linux](linux.md)
- [Building a release](releasing.md), [publishing it](publishing.md) and the [distribution layout](distribution.md)
- [Performance](performance.md)
- [Roadmap](roadmap.md) and [changelog](../CHANGELOG.md)
