# Building and testing on Linux

wood is tested on Debian 13 x86-64 with glibc, with GCC 14.2 and Clang 19.1 using libstdc++. Other
distributions, musl, libc++ and other architectures haven't been tested. To install a release instead, see
[installation](installation.md#linux).

## What you need

CMake 3.25 or newer, Ninja, and a C++23 compiler whose standard library has `std::jthread`, `std::stop_token` and floating
`std::to_chars`. CMake checks for these when you configure. On Debian 13:

```sh
sudo apt install cmake ninja-build g++ clang-19 libclang-rt-19-dev libssl3t64 libsqlite3-0
```

OpenSSL 3 and SQLite are only needed for the TLS and SQLite tests, and wood loads them at run time, so their development
headers aren't needed. `libclang-rt-19-dev` provides the sanitizer runtimes for Clang. None of this needs Python.

## The full check

`tools/validate-linux.sh` builds and tests one compiler in one configuration. Use a separate directory for each:

```sh
bash tools/validate-linux.sh g++ out/linux-gcc-release release
bash tools/validate-linux.sh clang++-19 out/linux-clang-release release
bash tools/validate-linux.sh g++ out/linux-gcc-sanitizers sanitizers
bash tools/validate-linux.sh clang++-19 out/linux-clang-sanitizers sanitizers
```

It uses two workers by default; set `WOOD_TEST_JOBS` (1 to 32) to change that. Python comparison tests are off unless you set
`WOOD_PYTHON_TESTS=ON`. Each run:

1. Records the OS, compiler, standard library, CMake and library versions in `environment.txt`.
2. Configures with Ninja, with `WOOD_CXX` set to the same compiler for the programs wood builds.
3. Builds and runs CTest, keeping text and JUnit reports. It fails if no tests were found, and with
   `WOOD_REQUIRE_TLS_TESTS=1`, if TLS couldn't be tested.
4. Installs wood, then uses the installed compiler to generate an OBJ mesh, build a mesh transformer, run it and check the
   result.
5. Checks the executable's dependencies with `readelf` and rejects a dependency on `libpython`.
6. Checks that bad input exits with code 70 and leaves an existing output file alone.
7. In the sanitizer configuration, also builds the emitted numerical and mesh code with ASan and UBSan and checks the results.
8. Writes `validation-complete.txt` only if everything passed.

The sanitizer configuration uses `RelWithDebInfo` and `WOOD_SANITIZERS=ON`. It's for finding bugs, not for timing.

## The mesh example by hand

With an install from the GCC run:

```sh
wood=./out/linux-gcc-release/install/bin/wood
mkdir -p out/mesh-demo
"$wood" examples/mesh_torus.wd -- out/mesh-demo/input.obj 32 16
"$wood" build examples/mesh_import.wd -o out/mesh-demo/mesh-import
./out/mesh-demo/mesh-import out/mesh-demo/input.obj out/mesh-demo/transformed.obj
"$wood" tests/mesh_transform_verify.wd -- out/mesh-demo/input.obj out/mesh-demo/transformed.obj
```

The checker verifies every coordinate against `2*x+1`, `2*y+2` and `2*z-3`, that the triangles are unchanged, and that the
surface area grew fourfold.

## Just the compiler

```sh
cmake -S . -B out/core-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DWOOD_PYTHON_TESTS=OFF
cmake --build out/core-gcc --target wood --parallel 2
./out/core-gcc/wood examples/hello.wd
```

This is the C++ reference compiler, which needs a C++ compiler to build programs; `wood check` and `wood inspect` don't.
`--cxx` or `WOOD_CXX` names the C++ compiler, as a single executable path with no flags. For custom flags, use `emit-cpp`
and compile the result yourself.

## Workflows

[`.github/workflows/linux.yml`](../.github/workflows/linux.yml) runs the same four checks on Ubuntu 24.04 with GCC 13 and
Clang 18, but GitHub Actions isn't running for this repository, so all testing happens locally.
