# The `wood-selfhost` driver

`wood-selfhost` is the [compiler written in wood](bootstrap-compiler.md) with its C++ backend, wrapped in the normal
commands. It is an optional part of a source build, used to compare the two backends. The installers ship
[native `wood`](native-cli.md) instead, which shares this driver.

Its frontend, C++ emitter, command-line parsing, project and lock handling, build orchestration and test runner are all
written in wood. A small native bridge supplies process and filesystem access and embeds the runtime, the standard library
and the error catalog; it doesn't link the C++ compiler's frontend. Building programs needs a C++23 compiler. Python is never
needed. [`driver-contract.json`](../bootstrap/driver-contract.json) describes what it supports.

## Build and install

~~~sh
cmake -S . -B build -DWOOD_SELFHOST_TESTS=ON
cmake --build build --config Release --target wood_selfhost wood_selfhost_host_tests --parallel 1
ctest --test-dir build -C Release -R '^selfhost_' --output-on-failure
cmake --install build --config Release --component SelfHosted --prefix /separate/prefix
~~~

Use a Visual Studio developer shell on Windows. The executable is under `build/selfhost-candidate/Release` with Visual
Studio, or `build/selfhost-candidate` otherwise. Installing puts `wood-selfhost` in `bin`, its bridge archive in `lib`, and
the compiler's sources, docs and examples in `share/wood`. Use a separate prefix: a normal install doesn't include it.

To bootstrap from a compiler you kept, set `WOOD_SELFHOST_EMITTER` to that four-argument emitter. Otherwise the first seed
comes from the reference compiler.

## Use it

~~~sh
wood-selfhost init my_app
wood-selfhost lock my_app
wood-selfhost check my_app --locked
wood-selfhost run my_app --locked -- "world"
wood-selfhost build my_app --locked
wood-selfhost test my_app --locked --report=json
wood-selfhost deps my_app --locked
wood-selfhost inspect my_app --locked
wood-selfhost explain E3030
wood-selfhost script.wd -- "program argument"
~~~

The C++ compiler is chosen from `--cxx`, then `WOOD_CXX`, then the one configured at build time, then the platform default.
It names one executable, not a command line with flags, and arguments go straight to the OS with no shell. `check`,
`inspect` and `explain` never run a C++ compiler. `-O0`, `-O2`, `--link-library`, `--sanitize=address`, timeouts and output
limits all work.

`lock` writes the lock only once it's complete, and builds never refresh it. `init` never overwrites existing files. Build
outputs are staged and moved into place only on success, and can't overwrite sources or project files.

## Rebuild it with itself

Use the bridge archive that matches the installed executable:

~~~sh
wood-selfhost build /prefix/share/wood/bootstrap/src/driver_main.wd \
  --link-library /prefix/lib/libwood_driver_host.a -O0 -o stage2
stage2 check /prefix/share/wood/bootstrap/src/driver_main.wd
~~~

With MSVC, use the `.lib` archive and an `.exe` output. The archive sets the 8 MiB stack the compiler needs and uses the
static CRT.
