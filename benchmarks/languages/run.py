#!/usr/bin/env python3
"""Compare wood with C++, Rust, Go, Java and Python on one machine.

Builds every program in programs/ in its optimized release form, then times whole
processes from outside and checks that every language printed the same answer.
See README.md. Run it on an otherwise idle machine.
"""

import argparse
import datetime
import json
import os
import pathlib
import platform
import random
import shutil
import statistics
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
PROGRAMS = HERE / "programs"

# Sizes are chosen so the slowest language stays practical and the fastest still
# runs long enough to time well. Changing one means measuring everything again.
WORKLOADS = {
    "startup": [],
    "fib": ["40"],
    "sieve": ["30000000"],
    "mandelbrot": ["1000", "100"],
    "nbody": ["1000000"],
    "matrix": ["500"],
    "sort": ["5000000"],
    "dict": ["5000000", "50021"],
    "strings": ["500000", "6"],
}

LANGUAGES = ["wood", "cpp", "rust", "go", "java", "python"]


def run(command, **options):
    return subprocess.run(command, check=True, capture_output=True, text=True, **options)


def msvc_environment(vswhere):
    root = run([vswhere, "-latest", "-products", "*", "-property", "installationPath"]).stdout.strip()
    script = pathlib.Path(root) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not script.is_file():
        raise SystemExit(f"cannot find {script}")
    output = subprocess.run(f'call "{script}" >nul && set', shell=True, check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in output.splitlines() if "=" in line)


def first_line(command, env=None):
    result = subprocess.run(command, capture_output=True, text=True, env=env)
    return (result.stdout + result.stderr).strip().splitlines()[0].strip()


class Tools:
    def __init__(self, options):
        self.wood = options.wood
        self.go = options.go
        self.python = options.python or sys.executable
        self.rustc = shutil.which("rustc")
        self.javac = shutil.which("javac")
        self.java = shutil.which("java")
        self.msvc = msvc_environment(options.vswhere)
        for name, tool in [("rustc", self.rustc), ("javac", self.javac), ("java", self.java)]:
            if tool is None:
                raise SystemExit(f"{name} is not on PATH")

    def versions(self):
        cl = subprocess.run(["cl"], capture_output=True, text=True, env=self.msvc, shell=True)
        return {
            "wood": first_line([self.wood, "--version"]),
            "cpp": cl.stderr.strip().splitlines()[0].strip(),
            "rust": first_line([self.rustc, "--version"]),
            "go": first_line([self.go, "version"]),
            "java": first_line([self.java, "--version"]),
            "python": first_line([self.python, "--version"]),
        }

    def flags(self):
        return {
            "wood": "wood build -O2",
            "cpp": "cl /O2 /GL /DNDEBUG /EHsc /MT /std:c++20 /link /LTCG",
            "rust": "rustc --edition 2021 -C opt-level=3 -C codegen-units=1 -C lto=fat -C panic=abort",
            "go": "go build (the default compiler optimizations)",
            "java": "javac, then java -cp (the default JIT and garbage collector)",
            "python": "python main.py (CPython, no NumPy and no extensions)",
        }

    def build(self, language, source_dir, out):
        """Return the command that runs the built program."""
        out.mkdir(parents=True, exist_ok=False)
        if language == "wood":
            exe = out / "main.exe"
            run([self.wood, "build", str(source_dir / "main.wd"), "-O2", "-o", str(exe)])
            return [str(exe)]
        if language == "cpp":
            run(["cl", "/nologo", "/O2", "/GL", "/DNDEBUG", "/EHsc", "/MT", "/std:c++20", str(source_dir / "main.cpp"),
                 "/link", "/LTCG", "/OUT:main.exe"], cwd=out, env=self.msvc, shell=True)
            return [str(out / "main.exe")]
        if language == "rust":
            exe = out / "main.exe"
            run([self.rustc, "--edition", "2021", "-C", "opt-level=3", "-C", "codegen-units=1", "-C", "lto=fat",
                 "-C", "panic=abort", str(source_dir / "main.rs"), "-o", str(exe)])
            return [str(exe)]
        if language == "go":
            exe = out / "main.exe"
            environment = dict(os.environ, GO111MODULE="off", GOCACHE=str(out / "gocache"), GOFLAGS="", GOTOOLCHAIN="local")
            run([self.go, "build", "-o", str(exe), str(source_dir / "main.go")], env=environment)
            return [str(exe)]
        if language == "java":
            run([self.javac, "-d", str(out), str(source_dir / "Main.java")])
            return [self.java, "-cp", str(out), "Main"]
        if language == "python":
            return [self.python, str(source_dir / "main.py")]
        raise SystemExit(f"unknown language {language}")


def time_once(command, arguments):
    start = time.perf_counter_ns()
    result = subprocess.run(command + arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    elapsed = time.perf_counter_ns() - start
    if result.returncode != 0:
        raise SystemExit(f"{command} exited with {result.returncode}: {result.stderr[:500]}")
    return elapsed / 1_000_000, result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wood", required=True, help="the wood executable to measure")
    parser.add_argument("--wood-source", help="the commit that wood was built from, when it is not a released build")
    parser.add_argument("--go", default=shutil.which("go"), help="the go executable")
    parser.add_argument("--python", help="the Python to measure (default: the one running this)")
    parser.add_argument("--vswhere", default=r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    parser.add_argument("--out", required=True, type=pathlib.Path, help="a new directory for the builds")
    parser.add_argument("--result", required=True, type=pathlib.Path, help="where to write the JSON result")
    parser.add_argument("--rounds", type=int, default=11, help="timed runs per program")
    parser.add_argument("--warmup", type=int, default=1, help="untimed runs per program first")
    parser.add_argument("--only", help="comma-separated workloads, to try things out")
    options = parser.parse_args()
    if options.go is None:
        raise SystemExit("go is not on PATH; pass --go")
    if options.out.exists():
        raise SystemExit(f"{options.out} already exists; choose a new directory")
    options.out = options.out.resolve()
    options.result = options.result.resolve()
    options.wood = str(pathlib.Path(options.wood).resolve())
    options.go = str(pathlib.Path(options.go).resolve())

    workloads = options.only.split(",") if options.only else list(WORKLOADS)
    tools = Tools(options)
    commands = {}
    for workload in workloads:
        for language in LANGUAGES:
            commands[workload, language] = tools.build(language, PROGRAMS / workload, options.out / workload / language)
    print("built everything", flush=True)

    # Each round runs every program once, in a fresh random order, so that a slow
    # spell on the machine lands on all languages rather than on one.
    shuffler = random.Random(20261103)
    pairs = list(commands)
    answers = {}
    for pair in pairs:
        for _ in range(options.warmup):
            answers[pair] = time_once(commands[pair], WORKLOADS[pair[0]])[1]
    for workload in workloads:
        seen = {answers[workload, language] for language in LANGUAGES}
        if len(seen) != 1:
            raise SystemExit(f"{workload}: languages disagree: " + str({language: answers[workload, language] for language in LANGUAGES}))
    print("every language agrees on every answer", flush=True)

    samples = {pair: [] for pair in pairs}
    began = datetime.datetime.now(datetime.timezone.utc)
    for number in range(options.rounds):
        shuffler.shuffle(pairs)
        for pair in pairs:
            elapsed, answer = time_once(commands[pair], WORKLOADS[pair[0]])
            if answer != answers[pair]:
                raise SystemExit(f"{pair} changed its answer")
            samples[pair].append(round(elapsed, 3))
        print(f"round {number + 1} of {options.rounds}", flush=True)

    results = {}
    for workload in workloads:
        rows = {}
        for language in LANGUAGES:
            values = samples[workload, language]
            rows[language] = {"median_ms": round(statistics.median(values), 3), "min_ms": min(values), "max_ms": max(values), "samples_ms": values}
        results[workload] = {"arguments": WORKLOADS[workload], "answer": answers[workload, LANGUAGES[0]], "languages": rows}

    options.result.parent.mkdir(parents=True, exist_ok=True)
    options.result.write_text(json.dumps({
        "format": 1,
        "began_utc": began.isoformat(timespec="seconds"),
        "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "system": {"os": platform.platform(), "machine": platform.machine(), "processor": os.environ.get("BENCH_CPU", platform.processor()), "logical_cpus": os.cpu_count()},
        "rounds": options.rounds,
        "warmup_runs": options.warmup,
        "wood_source": options.wood_source,
        "versions": tools.versions(),
        "flags": tools.flags(),
        "workloads": results,
    }, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {options.result}")


if __name__ == "__main__":
    main()
