#!/bin/sh
set -eu
if [ "$#" -ne 7 ]; then
    echo 'usage: qualify-linux.sh SETUP SETUP_SHA ARCHIVE ARCHIVE_SHA LLVM_DIR REFERENCE_NATIVE ABSENT_EVIDENCE' >&2
    exit 2
fi
setup=$1; setup_sha=$2; archive=$3; archive_sha=$4; tools=$5; reference=$6; evidence=$7
test "$(sha256sum "$setup" | cut -d ' ' -f 1)" = "$setup_sha"
test "$(sha256sum "$archive" | cut -d ' ' -f 1)" = "$archive_sha"
if [ -e "$evidence" ] || [ -L "$evidence" ]; then echo 'Preserve prior evidence; choose absent directory' >&2; exit 2; fi
case "$evidence" in /*) ;; *) exit 2;; esac
test -x "$tools/opt"; test -x "$tools/llc"; test -x /usr/bin/ld
export PATH="$tools:/usr/bin:/bin"
export WOOD_CXX=/deliberately-missing-cxx
mkdir -p "$evidence/extracted"
cd "$evidence"
count=0
run() {
    count=$((count+1))
    printf '\nCOMMAND %s:' "$count" >> commands.log
    printf ' %s' "$@" >> commands.log
    printf '\n' >> commands.log
    if "$@" > "command-$count.stdout" 2> "command-$count.stderr"; then
        printf 'PASS\n' >> commands.log
    else
        status=$?; printf 'FAIL %s\n' "$status" >> commands.log; exit "$status"
    fi
}
tar -xzf "$archive" -C extracted
(cd extracted/wood-native && sha256sum -c MANIFEST.sha256) > archive-manifest.log
stage1="$evidence/extracted/wood-native/bin/wood"
cat > active.wd <<'EOF'
arguments = args()
print(json_str(json_dict(json_parse(read_text(arguments[0])))["active"]))
EOF
root="$evidence/installation é"
run "$setup" --help
grep -q ' install DESTINATION$' "command-$count.stdout"
run "$setup" install "$root"
test "$(wc -l < "command-$count.stdout")" -le 8
grep -q 'export PATH="' "command-$count.stdout"
printf 'PASS: setup help works, and install prints a short summary with the PATH line\n' >> checks.txt
run "$stage1" run "$evidence/active.wd" -- "$root/receipt.json"
active=$(cat "command-$count.stdout")
case "$active" in versions/*) ;; *) echo 'Invalid generation output' >&2; exit 2;; esac
digest=${active#versions/}
test "${#digest}" -eq 64
case "$digest" in *[!a-f0-9]*) exit 2;; esac
generation="$root/$active"
cli="$generation/bin/wood"
data="$generation/share/wood"
cp "$cli" original-stage1
run "$cli" --version
grep -q '^wood 1\.1\.0 (language 0\.4\.0, native runtime ABI 4)$' "command-$count.stdout"
run "$cli" --language-info
grep -q '"driver_default_backend"[[:space:]]*:[[:space:]]*"llvm"' "command-$count.stdout"
run "$cli" init "$evidence/project"
for command in lock check deps inspect test; do run "$cli" "$command" "$evidence/project"; done
if "$cli" build "$evidence/project" --cxx /deliberately-missing-cxx > invalid-cxx.stdout 2> invalid-cxx.stderr; then
    echo 'Native builds must reject the C++-only option' >&2; exit 2
else
    test "$?" -eq 2
    grep -q -- '--cxx requires --backend=cpp' invalid-cxx.stderr
fi
printf 'PASS: invalid C++-only build option rejected with exit 2\n' >> checks.txt
run "$cli" build "$evidence/project" -o "$evidence/hello"
run "$evidence/hello"
run "$cli" run "$evidence/project"
for stage in 2 3; do
    run "$cli" build "$data/bootstrap/src/driver_main.wd" -O0 -o "$evidence/stage$stage"
    cp "$evidence/stage$stage" "$cli"
    run "$cli" emit-llvm "$data/bootstrap/src/driver_main.wd" -o "$evidence/stage$stage.ll"
done
cmp stage2 stage3
cmp stage2.ll stage3.ll
printf 'PASS: stages two/three executable and IR bytes identical\n' >> checks.txt
for relative in examples/mesh_import.wd tests/native_arrays.wd tests/native_callables.wd; do
    name=$(basename "$relative" .wd)
    run "$cli" emit-llvm "$data/$relative" -o "$evidence/$name.candidate.ll"
    run "$reference" emit-llvm "$data/$relative" -o "$evidence/$name.reference.ll"
    cmp "$name.candidate.ll" "$name.reference.ll"
    printf 'PASS: reference IR %s\n' "$relative" >> checks.txt
done
run "$cli" run "$data/examples/mesh_import.wd" -- "$data/tests/fixtures/obj/tetrahedron.obj" "$evidence/transformed.obj"
run "$cli" run "$data/examples/mesh_import.wd" -- "$evidence/transformed.obj" "$evidence/roundtrip.obj"
cat > mesh-check.wd <<'EOF'
import std.geometry as geo
import std.num as num
arguments = args()
original = geo.read_obj(arguments[0])
changed = geo.read_obj(arguments[1])
assert(num.shape(original.vertices)[0] == num.shape(changed.vertices)[0])
assert(num.shape(original.triangles)[0] == num.shape(changed.triangles)[0])
assert(num.close(geo.area(changed), 4.0 * geo.area(original)))
print("mesh transformation verified")
EOF
run "$cli" run "$evidence/mesh-check.wd" -- "$data/tests/fixtures/obj/tetrahedron.obj" "$evidence/transformed.obj"
printf 'PASS: real mesh import/transform/export/reimport and numerical area check\n' >> checks.txt
cp -a "$generation" "$evidence/relocated"
run "$evidence/relocated/bin/wood" run "$evidence/project"
printf 'PASS: relocated source/assets and native compilation\n' >> checks.txt
# Restore the declared owned binary after the intentional disposable bootstrap.
cp original-stage1 "$cli"
printf 'preserve\n' > "$root/user-created.txt"
printf 'user edit\n' >> "$generation/GETTING_STARTED.txt"
run "$setup" uninstall "$root" wood
test ! -e "$cli"
test ! -e "$root/receipt.json"
test "$(cat "$root/user-created.txt")" = preserve
grep -q 'user edit' "$generation/GETTING_STARTED.txt"
printf 'PASS: uninstall preserves additions/edits and removes owned executable\n' >> checks.txt
printf '{"passed_commands":%s,"failed_commands":0,"profile":"existing Linux host; external LLVM 19; no clean-VM claim"}\n' "$count" > result.json
echo "Linux archive/install/bootstrap qualification passed: $count commands"
