#!/bin/sh
# Builds the Linux release files and the source archive from a clean checkout. See docs/releasing.md.
# Usage: build-linux.sh SEED.tar.gz LLVM_BIN_DIRECTORY ABSENT_ABSOLUTE_OUTPUT [SEED_SHA256]
# Set CLANG to choose the compiler of the freestanding runtime object (default: clang on PATH).
set -eu
if [ "$#" -ne 3 ] && [ "$#" -ne 4 ]; then
    echo 'usage: build-linux.sh SEED.tar.gz LLVM_BIN_DIRECTORY ABSENT_ABSOLUTE_OUTPUT [SEED_SHA256]' >&2
    exit 2
fi
seed_archive=$1; tools=$2; out=$3; seed_override=${4:-}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
case "$out" in /*) ;; *) echo 'Output must be absolute' >&2; exit 2;; esac
case "$out" in "$root"/*) echo 'Keep the output outside the checkout' >&2; exit 2;; esac
if [ -e "$out" ] || [ -L "$out" ]; then echo "Output must not exist: $out" >&2; exit 2; fi
clang=${CLANG:-$(command -v clang || true)}
for tool in cmake ninja git gzip sha256sum tar /usr/bin/ld "$tools/opt" "$tools/llc" "$clang"; do
    command -v "$tool" > /dev/null 2>&1 || { echo "Missing tool: $tool. See docs/releasing.md" >&2; exit 2; }
done
cd "$root"
if [ -n "$(git status --porcelain)" ]; then echo 'The checkout must be clean' >&2; exit 2; fi
pinned=$(sed -n 's/.*"seed_linux_sha256": *"\([0-9a-f]*\)".*/\1/p' tools/release/inputs.json)
expected=${seed_override:-$pinned}
seed_sha=$(sha256sum "$seed_archive" | cut -d ' ' -f 1)
if [ "$seed_sha" != "$expected" ]; then echo "Seed archive hash $seed_sha is not the expected $expected" >&2; exit 2; fi

commit=$(git rev-parse HEAD)
short=$(git rev-parse --short=7 HEAD)
epoch=$(git log -1 --format=%ct)
mkdir -p "$out/release" "$out/tmp" "$out/seed" "$out/stage1-reference"
export TMPDIR="$out/tmp"
export PATH="$tools:/usr/bin:/bin"
export WOOD_CXX=/deliberately-missing-cxx
step() {
    name=$1; shift
    if "$@" > "$out/$name.log" 2>&1; then echo "PASS $name"; else echo "FAIL $name"; tail -20 "$out/$name.log"; exit 1; fi
}
echo "commit $commit epoch $epoch seed $seed_sha"
tar -xzf "$seed_archive" -C "$out/seed"
tar -xzf "$seed_archive" -C "$out/stage1-reference"
# The comparison is of what the compiler emits, and the runtime text is an input to that.
cp runtime/native/*.ll "$out/stage1-reference/wood-native/share/wood/native/"
seed="$out/seed/wood-native"
wood="$seed/bin/wood"
assets="$seed/share/wood"
# The standard library comes from this checkout, not the seed.
cmake "-DOUTPUT=$assets/stdlib.json" -P tools/bundle-stdlib.cmake
cmake "-DOUTPUT=$assets/explanations.json" -P tools/bundle-explanations.cmake

step runtime cmake "-DCLANG=$clang" -DTARGET=linux-x86_64 "-DOUTPUT=$out/managed.o" -P tools/prepare-native-runtime.cmake
cp -a runtime/native/. "$assets/native/"
cp "$out/managed.o" "$assets/native/managed.o"

# Two compiler generations reach the new runtime; generations two and three must then be identical.
driver=bootstrap/src/driver_main.wd
step stage1 "$wood" build "$driver" -O0 -o "$out/stage1"
cp "$out/stage1" "$wood.next"; mv "$wood.next" "$wood"
# The IR reference is this source built by the seed, a different route to the same compiler. It cannot be the
# seed itself: a change to what the compiler emits would then always fail.
cp "$out/stage1" "$out/stage1-reference/wood-native/bin/wood"
cp "$assets/stdlib.json" "$assets/explanations.json" "$out/stage1-reference/wood-native/share/wood/"
step stage2-ir "$wood" emit-llvm "$driver" -o "$out/stage2.ll"
step stage2 "$wood" build "$driver" -O0 -o "$out/stage2"
cp "$out/stage2" "$wood.next"; mv "$wood.next" "$wood"
step stage3-ir "$wood" emit-llvm "$driver" -o "$out/stage3.ll"
step stage3 "$wood" build "$driver" -O0 -o "$out/stage3"
cmp "$out/stage2.ll" "$out/stage3.ll"
cmp "$out/stage2" "$out/stage3"
echo 'PASS bootstrap stage two and three identical (IR and executable)'
cp "$out/stage3" "$wood.next"; mv "$wood.next" "$wood"

# The native adapter and the release tools, all from this checkout.
step adapter-configure cmake -G Ninja -S packages/wood_installer/native -B "$out/native" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
step adapter cmake --build "$out/native" --parallel 2
adapter="$out/native/libwood_installer_native.a"
step emitter "$wood" build bootstrap/src/release_native_main.wd -O0 -o "$out/release-emitter"
step compiler cmake "-DEMITTER=$out/release-emitter" "-DASSETS=$assets" "-DLLVM_DIRECTORY=$tools" "-DSOURCE_ROOT=$root" -DTARGET=linux-x86_64 -DLINKER=/usr/bin/ld "-DINPUT=$driver" "-DOUTPUT=$out/release-compiler" -DOPTIMIZATION=0 -P tools/installer/build-release-native.cmake
step template cmake "-DEMITTER=$out/release-emitter" "-DASSETS=$assets" "-DLLVM_DIRECTORY=$tools" "-DSOURCE_ROOT=$root" -DTARGET=linux-x86_64 -DLINKER=/usr/bin/ld "-DLIBRARIES=$adapter" -DINPUT=tools/installer/cli.wd "-DOUTPUT=$out/release-cli" -DOPTIMIZATION=2 -P tools/installer/build-release-native.cmake
step builder "$wood" build tools/installer/builder.wd -O2 --link-library "$adapter" -o "$out/installer-builder"

git archive --format=tar -o "$out/source-tree.tar" "$commit"
git archive --format=tar "--prefix=wood-$short/" -o "$out/source-release.tar" "$commit"
tree_sha=$(sha256sum "$out/source-tree.tar" | cut -d ' ' -f 1)
for run in package repro; do
    step "$run" sh tools/installer/build-linux.sh "$seed_archive" "$seed_sha" "$out/source-tree.tar" "$tree_sha" "$out/release-compiler" "$out/release-cli" "$out/installer-builder" "$epoch" "$out/$run" "$out/managed.o"
done
cmp "$out/package/wood-core-linux-x86_64.tar.gz" "$out/repro/wood-core-linux-x86_64.tar.gz"
cmp "$out/package/wood-setup-linux-x86_64" "$out/repro/wood-setup-linux-x86_64"
echo 'PASS archive and setup byte-identical across two builds'
gzip -n -c "$out/source-release.tar" > "$out/release/wood-source-$short.tar.gz"
gzip -n -c "$out/source-release.tar" > "$out/source-repeat.tar.gz"
cmp "$out/release/wood-source-$short.tar.gz" "$out/source-repeat.tar.gz"
echo 'PASS source archive byte-identical across two builds'
cp "$out/package/wood-core-linux-x86_64.tar.gz" "$out/package/wood-setup-linux-x86_64" "$out/release/"
pinned_flag=false
[ "$seed_sha" = "$pinned" ] && pinned_flag=true
printf '{"format":1,"target":"linux-x86_64","source_commit":"%s","source_epoch":%s,"source_tree_sha256":"%s","seed_sha256":"%s","seed_is_pinned_release":%s}\n' "$commit" "$epoch" "$tree_sha" "$seed_sha" "$pinned_flag" > "$out/build-info.json"
echo "LINUX BUILD DONE $out"
