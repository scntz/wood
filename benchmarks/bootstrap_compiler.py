"""Optional development-only whole-driver emission benchmark (no third parties).

All compared native compilers check and emit the same supported Wood programs. This is
not an isolated frontend or generated-program throughput comparison. Python is
only the measurement harness; neither compiler requires it.
"""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import time
import uuid
from pathlib import Path

from bootstrap_modules import peak_working_set


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--previous", type=Path, help="optional retained Wood-written compiler for an interleaved before/after comparison")
    parser.add_argument("--instrumented", action="store_true", help="all binaries use native_memory.hpp/application_wrapper.hpp and emit one metrics object on stderr; do not compare these timings with uninstrumented runs")
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("samples must be positive")
    if args.output.exists():
        parser.error("output already exists")
    scratch = args.output.parent / ("compiler-timing-" + uuid.uuid4().hex[:12])
    scratch.mkdir()
    implementations = ["wood", "cpp"] + (["previous"] if args.previous else [])
    binaries = [("wood", args.compiler), ("cpp", args.reference)]
    if args.previous:
        binaries.append(("previous", args.previous))
    evidence = {
        "format": 1,
        "platform": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(), "python_harness": platform.python_version(),
        "method": ("one discarded warmup each; rotating/reversed order" if args.previous else "one discarded warmup each; alternating order") + "; wall clock includes process startup, input decoding, complete checking, emission and file output; warm filesystem",
        "scope": "same supported input; production checks/emits full Wood through its optimized C++ frontend/backend; Wood compiler checks/emits its documented hosted profile through Wood stages; neither timing builds emitted C++",
        "memory": "Windows process peak working set; includes runtime, not live compiler heap; null on other systems",
        "allocations": ("global-new requests during complete application entry; bytes exclude counter headers/C allocator bookkeeping; instrumentation affects timings" if args.instrumented else "not measured"), "samples_per_implementation": args.samples,
        "binaries": {name: {"path": str(path.resolve()), "sha256": sha256(path)}
                     for name, path in binaries},
        "bundles": {name: {"path": str(path.resolve()), "sha256": sha256(path)}
                    for name, path in [("stdlib", args.stdlib), ("runtime", args.runtime)]},
        "cases": [],
    }
    for source in args.inputs:
        case = {"input": str(source.resolve()), "entry_sha256": sha256(source), "samples": [], "summary": {}}
        hashes = {}
        for sample in range(-1, args.samples):
            if args.previous:
                offset = sample % len(implementations)
                order = implementations[offset:] + implementations[:offset]
                if sample % 2:
                    order.reverse()
            else:
                order = ["wood", "cpp"] if sample % 2 == 0 else ["cpp", "wood"]
            for implementation in order:
                output = scratch / f"{len(evidence['cases'])}-{sample}-{implementation}.cpp"
                compiler = args.previous if implementation == "previous" else args.compiler
                command = ([str(compiler.resolve()), str(args.stdlib.resolve()),
                            str(args.runtime.resolve()), str(source.resolve()), str(output.resolve())]
                           if implementation != "cpp" else
                           [str(args.reference.resolve()), "emit-cpp", str(source.resolve()), "-o", str(output.resolve())])
                started = time.perf_counter_ns()
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                    stdout, stderr = process.communicate(timeout=300)
                    elapsed = time.perf_counter_ns() - started
                    peak = peak_working_set(process)
                    if process.returncode or stdout or (stderr and not args.instrumented):
                        raise RuntimeError((command, process.returncode, stdout, stderr))
                    instrumentation = json.loads(stderr) if args.instrumented else None
                    if instrumentation is not None and any(not isinstance(instrumentation.get(key), int) or instrumentation[key] < 0 for key in ["allocations", "allocated_bytes", "peak_bytes", "elapsed_ns"]):
                        raise RuntimeError("missing/invalid allocation instrumentation")
                digest = sha256(output)
                if hashes.setdefault(implementation, digest) != digest:
                    raise RuntimeError("non-deterministic output within implementation")
                if sample >= 0:
                    row = {"implementation": implementation, "sample": sample,
                                            "wall_ns": elapsed, "peak_working_set_bytes": peak,
                                            "output_bytes": output.stat().st_size, "output_sha256": digest}
                    if instrumentation is not None:
                        row["instrumentation"] = instrumentation
                    case["samples"].append(row)
        for implementation in implementations:
            samples = [s for s in case["samples"] if s["implementation"] == implementation]
            times = [s["wall_ns"] for s in samples]
            peaks = [s["peak_working_set_bytes"] for s in samples if s["peak_working_set_bytes"] is not None]
            case["summary"][implementation] = {"min_wall_ns": min(times), "median_wall_ns": statistics.median(times),
                                                "max_wall_ns": max(times), "max_peak_working_set_bytes": max(peaks) if peaks else None,
                                                "output_bytes": samples[0]["output_bytes"]}
            if args.instrumented:
                for key in ["allocations", "allocated_bytes", "peak_bytes"]:
                    values = [sample["instrumentation"][key] for sample in samples]
                    case["summary"][implementation]["min_"+key] = min(values)
                    case["summary"][implementation]["max_"+key] = max(values)
        case["summary"]["wood_cpp_median_ratio"] = case["summary"]["wood"]["median_wall_ns"] / case["summary"]["cpp"]["median_wall_ns"]
        if args.previous:
            case["summary"]["wood_previous_median_ratio"] = case["summary"]["wood"]["median_wall_ns"] / case["summary"]["previous"]["median_wall_ns"]
        evidence["cases"].append(case)
        print(json.dumps({"input": str(source), **case["summary"]}), flush=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
