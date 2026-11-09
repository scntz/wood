"""Build once, verify checksums, then time native/Python subprocesses."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

WORKLOADS = {
    "callbacks": ("reference_callbacks", "wood_bench_callbacks", "runtime-selected scalar function pointer with checked i64 arithmetic"),
    "sieve": ("reference", "wood_bench_reference", "prime sieve with checked i64 and bool lists"),
    "dictionary": ("reference_dictionary", "wood_bench_dictionary", "ordered dictionary counting with checked i64 and string keys"),
    "string_length": ("reference_string_length", "wood_bench_string_length", "repeated immutable string length with checked i64 accumulation"),
}
parser = argparse.ArgumentParser()
parser.add_argument("--workload", choices=WORKLOADS, default="sieve")
parser.add_argument("--wood", type=Path, required=True)
parser.add_argument("--cpp", type=Path, required=True)
parser.add_argument("--baseline-wood", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--size", type=int, default=1_000_000)
parser.add_argument("--iterations", type=int, default=2000)
parser.add_argument("--samples", type=int, default=7)
options = parser.parse_args()
if not 2 <= options.size <= 10_000_000 or options.samples < 3:
    parser.error("size must be 2..10000000 and samples must be at least 3")
if not 1 <= options.iterations <= 1_000_000:
    parser.error("iterations must be 1..1000000")
root = Path(__file__).resolve().parent
reference, target, description = WORKLOADS[options.workload]
options.output.parent.mkdir(parents=True, exist_ok=True)
program_arguments = [str(options.size)]
if options.workload == "string_length":
    data = options.output.parent / "string-input.bin"
    data.write_bytes(b"x" * options.size)
    program_arguments = [str(data.resolve()), str(options.iterations)]


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, timeout=180, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{command} failed ({result.returncode}):\n"
                           + (result.stdout + result.stderr).decode(errors="replace"))
    return result


def build(compiler, label):
    binary = options.output.parent / (label + (".exe" if os.name == "nt" else ""))
    command = [str(compiler.resolve()), "build", str(root / f"{options.workload}.wd"),
               "-O2", "-o", str(binary.resolve())]
    start = time.perf_counter()
    run(command)
    return binary, time.perf_counter() - start


binary, compile_seconds = build(options.wood, f"wood-{options.workload}")
commands = {
    "wood": [str(binary.resolve()), *program_arguments],
    "cpp_same_checked_runtime": [str(options.cpp.resolve()), *program_arguments],
    "python_same_algorithm": [sys.executable, str(root / f"{reference}.py"), *program_arguments],
}
artifacts = {"wood_compiler": options.wood, "wood_program": binary, "cpp_reference": options.cpp}
versions = {"wood": run([str(options.wood.resolve()), "--version"]).stdout.decode().strip()}
compile_times = {"wood": compile_seconds}
if options.baseline_wood:
    baseline, baseline_seconds = build(options.baseline_wood, f"baseline-{options.workload}")
    commands["wood_baseline"] = [str(baseline.resolve()), *program_arguments]
    artifacts.update({"baseline_compiler": options.baseline_wood, "baseline_program": baseline})
    versions["wood_baseline"] = run([str(options.baseline_wood.resolve()), "--version"]).stdout.decode().strip()
    compile_times["wood_baseline"] = baseline_seconds
expected = None
# One untimed warm-up per implementation, also verifying all outputs agree.
for command in commands.values():
    result = run(command).stdout.strip()
    if expected is None:
        expected = result
    if result != expected:
        raise RuntimeError(f"Incorrect checksum from {command}: {result!r} != {expected!r}")
known = None
if options.workload == "sieve" and options.size == 1_000_000:
    known = b"78498"
elif options.workload == "dictionary":
    known = f"{min(options.size,257)} {options.size}".encode()
elif options.workload == "string_length":
    known = str(options.size * options.iterations).encode()
elif options.workload == "callbacks":
    cycles, remainder = divmod(options.size, 256)
    square_sum = lambda n: n * (n - 1) * (2 * n - 1) // 6
    known = str(cycles * square_sum(256) + square_sum(remainder) + options.size).encode()
if known is not None and expected != known:
    raise RuntimeError(f"Incorrect known checksum: {expected!r} != {known!r}")
samples = {name: [] for name in commands}
rng = random.Random(20261017)
for _ in range(options.samples):
    names = list(commands)
    rng.shuffle(names)
    for name in names:
        start = time.perf_counter()
        result = run(commands[name])
        elapsed = time.perf_counter() - start
        if result.stdout.strip() != expected:
            raise RuntimeError(f"Incorrect checksum in {name} timing sample")
        samples[name].append(elapsed)

# Capture actual reference build settings when generated by CMake.
configuration, compiler_path = {}, None
for directory in options.cpp.resolve().parents:
    cache = directory / "CMakeCache.txt"
    if not cache.exists():
        continue
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("CMAKE_CXX_FLAGS", "CMAKE_GENERATOR:", "CMAKE_BUILD_TYPE:")):
            key, value = line.split("=", 1)
            configuration[key.split(":", 1)[0]] = value
    for compiler_file in (directory / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"):
        match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',
                          compiler_file.read_text(encoding="utf-8"))
        if match:
            compiler_path = match.group(1)
            break
    project = directory / f"{target}.vcxproj"
    if project.exists():
        ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
        for group in ET.parse(project).findall("ms:ItemDefinitionGroup", ns):
            if "'Release|" in group.get("Condition", ""):
                configuration["msvc_release_settings"] = {
                    element.tag.split("}")[-1]: element.text for element in group.find("ms:ClCompile", ns)
                }
    break
compiler_description = ["Compiler unavailable; retain the CMake build record"]
if compiler_path:
    name = Path(compiler_path).name.lower()
    flags = [] if name in ("cl", "cl.exe", "clang-cl", "clang-cl.exe") else ["--version"]
    compiler = subprocess.run([compiler_path, *flags], capture_output=True, timeout=10)
    compiler_description = (compiler.stdout + compiler.stderr).decode(errors="replace").splitlines()[:3]
source_files = [root / name for name in (f"{options.workload}.wd", f"{reference}.cpp", f"{reference}.py", "run.py")]
source_files.extend([root.parent / "runtime/wood_runtime.hpp", root.parent / "include/wood/json.hpp"])
report = {
    "schema_version": 1, "workload": description, "size": options.size,
    "iterations": options.iterations if options.workload == "string_length" else None,
    "checksum": expected.decode(), "samples": options.samples,
    "platform": platform.platform(), "machine": platform.machine(), "cpu": platform.processor(),
    "python": sys.version, "compiler": compiler_description, "versions": versions,
    "compile_seconds": compile_times,
    "source_sha256": {path.relative_to(root.parent).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_files},
    "executable_sha256": {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in artifacts.items()},
    "build_configuration": {"wood": "-O2; native flags documented in src/main.cpp", "cpp_reference": configuration},
    "timings": {name: {"median_seconds": statistics.median(values), "min_seconds": min(values), "max_seconds": max(values), "raw_seconds": values} for name, values in samples.items()},
    "limits": [
        "One workload per report on one host; not language-wide parity evidence",
        "Execution samples include process startup and output capture",
        "String workload also includes file reading; dictionary uses a tree index, Python a hash map",
        "C++ uses Wood's checked runtime and CMake Release flags",
        "Python uses arbitrary-precision integers and its own containers",
        "No peak memory or isolated frontend timing measured",
    ],
}
options.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"workload": options.workload, "versions": versions, "checksum": expected.decode(),
                  "compile_seconds": compile_times, "timings": report["timings"]}, indent=2))
