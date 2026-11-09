#!/bin/sh
# Qualifies a tools/release/build-linux.sh output. See docs/releasing.md.
# Usage: qualify-linux.sh BUILD LLVM_BIN_DIRECTORY [NODE] [CURL_BIN_DIRECTORY] [CHECK...]
# Every check runs and results are appended, so a failed attempt stays in the record beside its rerun.
set -u
if [ "$#" -lt 2 ]; then
    echo 'usage: qualify-linux.sh BUILD LLVM_BIN_DIRECTORY [NODE] [CURL_BIN_DIRECTORY] [CHECK...]' >&2
    exit 2
fi
build=$1; tools=$2; node=${3:-node}; curl_bin=${4:-}
[ "$#" -ge 2 ] && shift 2
[ "$#" -ge 1 ] && shift
[ "$#" -ge 1 ] && shift
only=" $* "
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root" || exit 1
stamp=$(date +%Y%m%d-%H%M%S)
evidence="$build/qualify-$stamp"
mkdir -p "$evidence"
results="$build/qualification-linux.txt"
base_path="$tools:/usr/bin:/bin"
export PATH="$base_path"
export WOOD_CXX=/deliberately-missing-cxx
export TMPDIR="$build/tmp"
commit=$(git rev-parse HEAD)
pkg="$build/package"
setup="$pkg/wood-setup-linux-x86_64"
archive="$pkg/wood-core-linux-x86_64.tar.gz"
prefix="$pkg/consumer/wood-native"
data="$prefix/share/wood"
wood="$prefix/bin/wood"
# Package checks need curl, which may be private to the test environment. They must not silently skip.
with_curl() {
    if [ -n "$curl_bin" ]; then
        PATH="$tools:$curl_bin:/usr/bin:/bin" LD_LIBRARY_PATH="$curl_bin/../lib/x86_64-linux-gnu" "$@"
    else
        "$@"
    fi
}
record() { line="$1 linux $2 $3"; printf '%s\n' "$line" >> "$results"; printf '%s   [%s]\n' "$line" "$(date +%H:%M:%S)"; }
# Passes only when the command exits zero and, if given, the last lines of its log have the expected text.
check() {
    name=$1; expect=$2; shift 2
    if [ "$only" != "  " ]; then case "$only" in *" $name "*) ;; *) return 0;; esac; fi
    log="$evidence/$name.log"
    "$@" > "$log" 2>&1
    code=$?
    found=""
    if [ -n "$expect" ]; then found=$(tail -40 "$log" | grep -E -o "$expect" | tail -1); fi
    if [ "$code" = 0 ] && { [ -z "$expect" ] || [ -n "$found" ]; }; then
        record PASS "$name" "${found:-exit 0}"
    else
        record FAIL "$name" "exit $code; see $log; $(tail -1 "$log" | cut -c1-180)"
    fi
}
echo "commit $commit, evidence $evidence"
[ "$only" = "  " ] && echo "RUN linux $stamp commit $commit" >> "$results"

installer() {
    sh "$root/tools/installer/qualify-linux.sh" "$setup" "$(sha256sum "$setup" | cut -d ' ' -f 1)" "$archive" "$(sha256sum "$archive" | cut -d ' ' -f 1)" "$tools" "$build/stage1-reference/wood-native/bin/wood" "$evidence/installer" || return $?
    echo "INSTALLER-OK $(grep -c '^PASS' "$evidence/installer/commands.log") commands"
}
check installer 'INSTALLER-OK [0-9]+ commands' installer
check version '^wood 1\.1\.0 \(language 0\.4\.0, native runtime ABI 4\)' "$wood" --version
services() {
    WOOD_REQUIRE_TLS=1 SSL_CERT_FILE="$root/tests/fixtures/tls/ca.pem" SSL_CERT_DIR="$evidence/no-extra-roots" \
        cmake "-DPREFIX=$prefix" "-DTEST_DIR=$evidence/services" "-DLLVM_DIR=$tools" -DLINKER=/usr/bin/ld -P "$data/tools/qualify-native-consumer.cmake"
}
check services-tls '' services
check community '' cmake "-DWOOD=$wood" "-DSOURCE_DIR=$data" "-DTEST_DIR=$evidence/community" -P "$data/tests/community_packages.cmake"
check documentation '' cmake "-DWOOD=$wood" "-DSOURCE_DIR=$data" "-DTEST_DIR=$evidence/documentation" -DDOCS_NATIVE_FRONTEND=ON "-DDOCS_GUIDES=README.md;docs/libraries.md" -P "$data/tests/documentation.cmake"
check diagnostics 'Diagnostics quality passed: [0-9]+ checks' cmake "-DWOOD=$wood" "-DWORK=$evidence/diagnostics" -P tests/diagnostics_quality.cmake
check heap '' "$wood" run tests/runtime_heap.wd
check math 'math functions ok' "$wood" run tests/math_functions.wd
check planted-programs 'Planted programs passed' cmake "-DWOOD=$wood" "-DROOT=$root" "-DTEST_DIR=$evidence/planted-programs" -P tests/planted_programs.cmake
check explanations 'Explanations passed' cmake "-DWOOD=$wood" "-DROOT=$root" "-DTEST_DIR=$evidence/explanations" -P tests/explanations.cmake
mkdir -p "$evidence/fuzz"
check fuzz-parsers 'fuzz parsers ok' "$wood" run tests/fuzz/parsers.wd -- 1 2000 "$evidence/fuzz"
check fuzz-compiler ' 0 crashes' "$node" tests/fuzz/compiler.mjs "$wood" 1 120 "$evidence/fuzz/compiler" tests examples tools/playground/examples stdlib
check soak 'soak ok' "$wood" run tests/soak.wd -- 120 "$evidence/fuzz"
lsp() { WOOD="$wood" "$node" --test --test-timeout=240000 tests/lsp/protocol.test.mjs; }
check lsp 'pass [0-9]+' lsp
command -v curl > /dev/null 2>&1 || [ -n "$curl_bin" ] || echo 'WARNING: no curl, so archive-source checks will skip'
check package-manager 'Package manager workflow passed: [0-9]+ checks' with_curl cmake "-DWOOD=$wood" "-DWORK=$evidence/pm" "-DSOURCE_DIR=$root" -P tests/package_manager.cmake
check official-fetch 'Official package fetch passed: [0-9]+ packages, [0-9]+ commands' with_curl cmake "-DWOOD=$wood" "-DWORK=$evidence/official-fetch" "-DSOURCE_DIR=$root" -P tests/official_fetch.cmake
python_flow() {
    with_curl cmake "-DWOOD=$wood" "-DWORK=$evidence/python" "-DEXTRA_PATH=$tools${curl_bin:+:$curl_bin}" -P tests/package_python.cmake
}
check python 'Python provider workflow passed: [0-9]+ commands' python_flow
for package in wood_process wood_audio wood_image wood_log wood_config wood_cli wood_installer; do
    check "package-$package" '[1-9][0-9]* passed, 0 failed' "$wood" test "packages/$package"
done
image_tool() {
    "$wood" build examples/image_tool -o "$evidence/image_tool" && "$evidence/image_tool" draw "$evidence/card.png" && "$evidence/image_tool" resize "$evidence/card.png" "$evidence/big.png" 320 200 && test -s "$evidence/big.png" && echo IMAGE-OK
}
check image-tool 'IMAGE-OK' image_tool
sanitizers() {
    unset WOOD_CXX
    PATH=/usr/bin:/bin cmake -S . -B "$evidence/asan" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DWOOD_SANITIZERS=ON -DWOOD_PYTHON_TESTS=OFF || return $?
    PATH=/usr/bin:/bin cmake --build "$evidence/asan" --parallel 3 || return $?
    PATH=/usr/bin:/bin ctest --test-dir "$evidence/asan" --output-on-failure --timeout 1500 -R '^(project_unit|project_init|compiler|projects)$'
}
check sanitizers 'tests passed' sanitizers
echo "LINUX QUALIFY DONE $results"
