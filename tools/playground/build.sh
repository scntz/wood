#!/bin/sh
# Builds the browser playground's WebAssembly compiler and runtime into a new directory.
# usage: tools/playground/build.sh OUTPUT
# WOOD selects the native compiler, CLANG a clang 19 with the WebAssembly target,
# WASM_LD wasm-ld 19, and STDLIB the stdlib.json of WOOD's installation.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
if [ $# -ne 1 ]; then echo "usage: tools/playground/build.sh OUTPUT" >&2; exit 2; fi
out=$1
if [ -e "$out" ]; then echo "output already exists: $out" >&2; exit 2; fi
wood=${WOOD:-wood}
clang=${CLANG:-clang-19}
wasm_ld=${WASM_LD:-wasm-ld-19}
stdlib=${STDLIB:-$(dirname "$(command -v "$wood")")/../share/wood/stdlib.json}
native=$root/runtime/native
work=$out/.work
mkdir -p "$work"
flags="--target=wasm64-unknown-unknown -O2 -mno-multivalue -mno-bulk-memory -mno-reference-types"

"$wood" build -O0 "$root/bootstrap/src/playground_main.wd" -o "$work/playground"
"$wood" build -O0 "$root/tools/playground/lower_memory64.wd" -o "$work/lower"
"$work/playground" llvm "$stdlib" "$native" "$root/bootstrap/src/playground_main.wd" "$work/compiler.ll"

"$clang" -x c $flags -std=c17 -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
    -c "$native/managed.c" -o "$work/managed.o"
# The playground reads errors as JSON.
cat "$native/wasm64-browser.ll" "$native/core.ll" | sed 's/@wd.json_errors = private constant i1 false/@wd.json_errors = private constant i1 true/' > "$work/core.ll"
"$clang" $flags -Wno-override-module -c "$work/core.ll" -o "$work/core.o"
# Without the inlining limit one compiler function outgrows what browsers accept.
"$clang" --target=wasm64-unknown-unknown -O1 -mllvm -inline-threshold=0 -mno-multivalue -mno-bulk-memory -mno-reference-types -Wno-override-module -c "$work/compiler.ll" -o "$work/compiler.o"

"$wasm_ld" -mwasm64 --no-entry --import-memory --stack-first -z stack-size=8388608 --gc-sections --compress-relocations --strip-debug \
    --export=wd_main --export=wd_m_args_init --export=wd_platform_alloc --export=__heap_base \
    "$work/managed.o" "$work/compiler.o" -o "$work/compiler64.wasm"
"$wasm_ld" -mwasm64 --no-entry --import-memory --stack-first -z stack-size=1048576 \
    --export-all --export-table --growable-table \
    "$work/managed.o" "$work/core.o" -o "$work/runtime64.wasm"

"$work/lower" "$work/compiler64.wasm" "$out/wood-compiler.wasm"
"$work/lower" "$work/runtime64.wasm" "$out/wood-runtime.wasm"
cp "$stdlib" "$out/stdlib.json"
cp "$native/wasm64-browser.ll" "$native/core.ll" "$out/"
rm -rf "$work"
ls -l "$out"
