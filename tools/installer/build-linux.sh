#!/bin/sh
# Build-machine tool, not a user install script. No global configuration changes.
set -eu
if [ "$#" -ne 9 ] && [ "$#" -ne 10 ]; then
    echo 'usage: build-linux.sh CORE.tar.gz CORE_SHA256 SOURCE.tar SOURCE_SHA256 COMPILER TEMPLATE BUILDER SOURCE_EPOCH ABSENT_WORK' >&2
    exit 2
fi
core=$1; core_sha=$2; sources=$3; source_sha=$4
compiler=$5; template=$6; builder=$7; epoch=$8; work=$9
runtime_object=${10:-}
case "$work" in /*) ;; *) echo 'Work must be absolute' >&2; exit 2;; esac
if [ -e "$work" ] || [ -L "$work" ]; then echo 'Preserve existing outputs; choose absent Work' >&2; exit 2; fi
test "$(sha256sum "$core" | cut -d ' ' -f 1)" = "$core_sha"
test "$(sha256sum "$sources" | cut -d ' ' -f 1)" = "$source_sha"
source_commit=$(git get-tar-commit-id < "$sources" 2>/dev/null || true)
mkdir -p "$work/base" "$work/input/wood-native/bin" "$work/input/wood-native/share/wood"
tar -xzf "$core" -C "$work/base"
payload="$work/input/wood-native"
data="$payload/share/wood"
tar -xf "$sources" -C "$data"
cp -a "$work/base/wood-native/share/wood/native" "$data/native"
if [ -n "$runtime_object" ]; then
    # This release changes the runtime, so the baseline's copy would not match the compiler.
    cp -a "$data/runtime/native/." "$data/native/"
    cp "$runtime_object" "$data/native/managed.o"
fi
for name in stdlib.json language.json explanations.json runtime.hpp; do
    if [ -f "$work/base/wood-native/share/wood/$name" ]; then cp "$work/base/wood-native/share/wood/$name" "$data/$name"; fi
done
if [ -n "$runtime_object" ]; then
    cp "$data/docs/language.json" "$data/language.json"
    cmake "-DOUTPUT=$data/stdlib.json" -P "$(dirname "$0")/../bundle-stdlib.cmake"
    cmake "-DOUTPUT=$data/explanations.json" -P "$(dirname "$0")/../bundle-explanations.cmake"
fi
cp "$compiler" "$payload/bin/wood"
cp "$template" "$payload/bin/wood-uninstall"
chmod 755 "$payload/bin/wood" "$payload/bin/wood-uninstall"
printf '%s\n' '{"format":1,"runtime_abi":4,"target":"linux-x86_64","llvm_dir":"","linker":"/usr/bin/ld","llvm_wsl":""}' > "$data/native.json"
printf '%s\n' "{\"format\":1,\"profile\":\"wood-core-local-preview\",\"distribution_version\":\"1.1.0\",\"language_version\":\"0.4.0\",\"target\":\"linux-x86_64\",\"runtime_abi\":4,\"core_archive_sha256\":\"$core_sha\",\"source_archive_sha256\":\"$source_sha\",\"source_commit\":\"$source_commit\",\"dependencies\":\"Operator-provided LLVM 19 opt/llc on PATH and GNU ld; optional OpenSSL 3 and SQLite 3.38+\"}" > "$payload/distribution.json"
cat > "$payload/GETTING_STARTED.txt" <<'EOF'
wood Core native preview — Linux x86-64

Provide LLVM 19 opt/llc on PATH and GNU ld (/usr/bin/ld). On Debian 13:
  sudo apt install llvm-19 binutils
  export PATH="/usr/lib/llvm-19/bin:$PATH"
No C++ compiler, Python or C/C++ application runtime is needed by ordinary builds.
LLVM host tools can need their distribution's system runtime libraries.
Optional services: OpenSSL 3 for TLS/HTTPS, SQLite 3.38+ for databases.
The C++ reference is a separate source build; its optional runtime.hpp sidecar
is not included in this LLVM-native bundle.

Keep the extracted tree intact. Use bin/wood --version, bin/wood init hello,
bin/wood check hello and bin/wood run hello. --llvm-dir selects a tool directory.
For installation use the accompanying native setup CLI:
  ./wood-setup-linux-x86_64 install /absolute/empty-or-managed/root
The parent must exist. Installation prints the exact PATH line to add to your shell profile.
No shell startup files are edited.
Remove using the downloaded setup (or installed bin/wood-uninstall):
  ./wood-setup-linux-x86_64 uninstall /absolute/managed/root wood
Unknown/edited files and a closed .setup-lock are preserved.

This is a locally qualified preview, not universal production readiness.
EOF
# Use the established deterministic archive/consumer verifier, not another format.
cmake -DPREFIX="$payload" -DOUTPUT="$work/wood-core-linux-x86_64.tar.gz" \
    -DSOURCE_DATE_EPOCH="$epoch" -P "$data/tools/package-native.cmake" > "$work/archive.log" 2>&1
mkdir "$work/consumer"
tar -xzf "$work/wood-core-linux-x86_64.tar.gz" -C "$work/consumer"
cat > "$work/application.json" <<'EOF'
{"format":1,"id":"wood","name":"wood","version":"1.1.0","publisher":"Wood","architecture":"linux-x86_64","license":"MIT OR Apache-2.0","command":"bin/wood","path_default":false,"desktop":"","start_menu":""}
EOF
"$builder" "$template" "$work/application.json" "$work/consumer/wood-native" "$work/wood-setup-linux-x86_64" > "$work/setup-build.log" 2>&1
(cd "$work" && sha256sum wood-core-linux-x86_64.tar.gz wood-setup-linux-x86_64 > SHA256SUMS)
echo "Prepared Linux preview: $work"
