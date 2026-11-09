#!/bin/sh
# Cross-check one wood program against g++ and clang++ on Linux, where Rust, Go and Java
# may not be installed. Usage: check_linux.sh WOOD WORK WORKLOAD ROUNDS [ARGUMENTS...]
# WORK must be a new directory. Prints the median and range for each, in milliseconds.
set -eu
wood=$1
work=$2
workload=$3
rounds=$4
shift 4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -e "$work" ]; then
    echo "$work already exists; choose a new directory" >&2
    exit 2
fi
mkdir -p "$work"
source_dir="$here/programs/$workload"
"$wood" build "$source_dir/main.wd" -O2 -o "$work/wood"
g++ -O3 -DNDEBUG -std=c++20 "$source_dir/main.cpp" -o "$work/gcc"
g++ -O2 -DNDEBUG -std=c++20 "$source_dir/main.cpp" -o "$work/gcc_o2"
clang++ -O3 -DNDEBUG -std=c++20 "$source_dir/main.cpp" -o "$work/clang"

"$work/wood" --version >/dev/null 2>&1 || true
"$wood" --version | head -1
g++ --version | head -1
clang++ --version | head -1
expected=$("$work/wood" "$@")
for name in wood gcc gcc_o2 clang; do
    [ "$("$work/$name" "$@")" = "$expected" ] || { echo "$name disagrees" >&2; exit 1; }
done
echo "every program printed $expected"

round=0
while [ "$round" -lt "$rounds" ]; do
    for name in wood clang gcc gcc_o2; do
        begin=$(date +%s%N)
        "$work/$name" "$@" >/dev/null
        end=$(date +%s%N)
        echo "$name $(( (end - begin) / 1000 ))" >> "$work/samples"
    done
    round=$((round + 1))
done

for name in wood gcc gcc_o2 clang; do
    grep "^$name " "$work/samples" | cut -d' ' -f2 | sort -n | awk -v name="$name" '
        { values[NR] = $1 }
        END { printf "%-7s median %.1f ms, range %.1f to %.1f ms, %d runs\n", name, values[int((NR + 1) / 2)] / 1000, values[1] / 1000, values[NR] / 1000, NR }'
done
