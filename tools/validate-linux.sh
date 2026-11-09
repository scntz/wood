#!/usr/bin/env bash
# Development gate only. Normal Wood compilation/execution does not use Bash.
set -euo pipefail

usage() {
    printf '%s\n' 'Usage: bash tools/validate-linux.sh CXX BUILD_DIR release|sanitizers' \
        'Environment: WOOD_TEST_JOBS=2 (1..32), WOOD_PYTHON_TESTS=OFF (ON or OFF).'
}
if [[ $# == 1 && $1 == --help ]]; then
    usage
    exit 0
fi
if [[ $# != 3 ]]; then
    usage >&2
    exit 2
fi
case "$3" in
    release) configuration=Release; sanitizers=OFF ;;
    sanitizers) configuration=RelWithDebInfo; sanitizers=ON ;;
    *) usage >&2; exit 2 ;;
esac
if [[ $(uname -s) != Linux ]]; then
    printf '%s\n' 'This gate requires actual Linux execution; no cross-platform simulation is performed.' >&2
    exit 2
fi
cxx=$(command -v -- "$1") || { printf 'Compiler not found: %s\n' "$1" >&2; exit 2; }
# Preserve the driver name (clang++ may be a symlink to clang), but make a
# relative executable independent of CTest's per-suite working directories.
if [[ $cxx != /* ]]; then
    cxx="$(cd -- "$(dirname -- "$cxx")" && pwd -P)/$(basename -- "$cxx")"
fi
if [[ ! -f $cxx || ! -x $cxx ]]; then
    printf '%s\n' 'CXX must identify an executable, not a shell function or alias' >&2
    exit 2
fi
jobs=${WOOD_TEST_JOBS:-2}
if [[ ! $jobs =~ ^[1-9][0-9]?$ ]] || (( jobs > 32 )); then
    printf '%s\n' 'WOOD_TEST_JOBS must be in 1..32' >&2
    exit 2
fi
python_tests=${WOOD_PYTHON_TESTS:-OFF}
if [[ $python_tests != ON && $python_tests != OFF ]]; then
    printf '%s\n' 'WOOD_PYTHON_TESTS must be ON or OFF' >&2
    exit 2
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
cmake -E make_directory "$2"
build=$(cd -- "$2" && pwd -P)
cmake -E rm -f "$build/validation-complete.txt"
export WOOD_CXX="$cxx"
export WOOD_REQUIRE_TLS_TESTS=1
if [[ $sanitizers == ON ]]; then
    export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}
    export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
fi
{
    date -u +'%Y-%m-%dT%H:%M:%SZ'
    uname -a
    "$cxx" --version
    "$cxx" -v -E -x c++ /dev/null 2>&1
    cmake --version
    printf 'Configuration: %s\nPython differential tests: %s\n' "$configuration" "$python_tests"
    printf 'ASAN_OPTIONS: %s\nUBSAN_OPTIONS: %s\n' "${ASAN_OPTIONS:-unset}" "${UBSAN_OPTIONS:-unset}"
    if command -v getconf >/dev/null; then getconf GNU_LIBC_VERSION || true; fi
    if command -v dpkg-query >/dev/null; then
        dpkg-query -W libc6 libstdc++6 libssl3t64 libsqlite3-0 2>/dev/null || true
    fi
} > "$build/environment.txt"
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$configuration" -DCMAKE_CXX_COMPILER="$cxx" \
    -DBUILD_TESTING=ON -DWOOD_SANITIZERS="$sanitizers" -DWOOD_PYTHON_TESTS="$python_tests"
cmake --build "$build" --config "$configuration" --parallel "$jobs"
ctest --test-dir "$build" -C "$configuration" --parallel "$jobs" \
    --no-tests=error --output-on-failure --output-log "$build/ctest.log" --output-junit "$build/ctest.xml"
cmake --install "$build" --config "$configuration" --prefix "$build/install"

# Run the installed compiler and a standalone native application, using Unicode
# paths and the same independent coordinate oracle as the cross-platform CTest.
wood="$build/install/bin/wood"
doc="$build/install/share/doc/wood"
demo="$build/native OBJ á 🌳"
cmake -E make_directory "$demo"
native_flags=()
if [[ $sanitizers == ON ]]; then native_flags+=(--sanitize=address); fi
"$wood" "$doc/examples/mesh_torus.wd" "${native_flags[@]}" -- "$demo/input.obj" 32 16 \
    > "$demo/input-report.json"
"$wood" build "$doc/examples/mesh_import.wd" "${native_flags[@]}" -o "$demo/mesh-import"
magic=$(od -An -tx1 -N4 "$demo/mesh-import" | tr -d ' \n')
if [[ $magic != 7f454c46 ]]; then
    printf '%s\n' 'Generated application is not an ELF executable' >&2
    exit 1
fi
printf '%s\n' "$magic" > "$demo/elf-magic.txt"
"$demo/mesh-import" "$demo/input.obj" "$demo/transformed.obj" > "$demo/workflow.log"
"$wood" "$root/tests/mesh_transform_verify.wd" "${native_flags[@]}" -- \
    "$demo/input.obj" "$demo/transformed.obj" > "$demo/verification.log"
printf 'f 1 2 3\n' > "$demo/malformed.obj"
printf 'preserve' > "$demo/preserved.obj"
if "$demo/mesh-import" "$demo/malformed.obj" "$demo/preserved.obj" \
    > "$demo/invalid.log" 2>&1; then
    printf '%s\n' 'Malformed OBJ was accepted' >&2
    exit 1
else
    status=$?
fi
if [[ $status != 70 || $(cat "$demo/preserved.obj") != preserve ]]; then
    printf '%s\n' 'Invalid import did not preserve the existing output' >&2
    exit 1
fi
if command -v readelf >/dev/null; then
    readelf -d "$demo/mesh-import" > "$demo/dynamic-dependencies.txt"
    if grep -i 'libpython' "$demo/dynamic-dependencies.txt"; then
        printf '%s\n' 'Unexpected Python runtime dependency' >&2
        exit 1
    fi
fi
if [[ $sanitizers == ON ]]; then
    # The public --sanitize=address option retains its existing semantics. Check
    # emitted numerical and mesh code with BOTH ASan and UBSan independently.
    for source in "$root/tests/native_computing.wd" "$doc/examples/mesh_import.wd"; do
        name=$(basename "$source" .wd)
        "$wood" emit-cpp "$source" -o "$demo/$name.cpp"
        "$cxx" -std=c++23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
            "$demo/$name.cpp" -pthread -ldl -o "$demo/$name-sanitized"
    done
    (cd -- "$demo" && ./native_computing-sanitized) > "$demo/numerical-sanitizers.log"
    "$demo/mesh_import-sanitized" "$demo/input.obj" "$demo/sanitized.obj" > "$demo/mesh-sanitizers.log"
    "$wood" "$root/tests/mesh_transform_verify.wd" "${native_flags[@]}" -- \
        "$demo/input.obj" "$demo/sanitized.obj" > "$demo/sanitized-verification.log"
fi
cmake -E sha256sum "$wood" "$doc/stdlib/geometry.wd" "$demo/mesh-import" \
    "$demo/input.obj" "$demo/transformed.obj" > "$build/artifact-sha256.txt"
printf '%s\n' 'Linux build, regressions, installation and native OBJ workflow passed.' \
    | tee "$build/validation-complete.txt"
