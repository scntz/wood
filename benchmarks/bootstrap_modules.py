"""Optional development-only timing orchestrator; no third-party packages.

Build the matching module_measure_main or signature_measure_main and C++ stage
oracle first. Measures native in-process warm filesystem/project/frontend work,
not process startup or JSON. Both implementations validate real module counts
and declaration counts; --stage records the timed semantic boundary.
"""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path


def peak_working_set(process):
    """Optional Windows process high-water mark, not live AST/heap bytes."""
    if platform.system() != "Windows":
        return None
    import ctypes
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
            "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    query = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
    query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    query.restype = wintypes.BOOL
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    if query(wintypes.HANDLE(int(process._handle)), ctypes.byref(counters), ctypes.sizeof(counters)):
        return counters.PeakWorkingSetSize
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wood", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--stage", choices=["module-bindings", "declaration-signatures"], default="module-bindings")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10000 or args.samples < 1:
        parser.error("iterations must be 1..10000 and samples must be positive")
    evidence = {
        "format": 1, "platform": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(),
        "binaries": {name: {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                     for name, path in [("wood", args.wood), ("cpp", args.reference)]},
        "stdlib_sha256": hashlib.sha256(args.stdlib.read_bytes()).hexdigest(),
        "python": platform.python_version(), "iterations_per_sample": args.iterations,
        "samples_per_implementation": args.samples,
        "method": ("one warmup per process; timed project/module load+lex+parse+bind"
                   + ("+signature resolution+unspecialized declaration checks" if args.stage == "declaration-signatures" else "")
                   + "+destruction; warm filesystem; excludes bundle decoding and output; alternating implementation order"),
        "stage": args.stage,
        "scope": ("module binding only; neither implementation checks bodies/types/locks" if args.stage == "module-bindings"
                  else "project/module load, lex/parse/bind, signature resolution, unspecialized declaration checks; no bodies, inference, specialization or locks"),
        "memory": "Windows process peak working set if available; includes runtime/bundle/warmup, not AST bytes; unavailable on other hosts",
        "allocations": "not measured", "cases": [],
    }
    for source in args.inputs:
        case = {"input": str(source.resolve()),
                "entry_sha256": hashlib.sha256(source.read_bytes()).hexdigest() if source.is_file() else None,
                "samples": [], "summary": {}}
        checksum = None
        for sample in range(args.samples):
            order = ["wood", "cpp"] if sample % 2 == 0 else ["cpp", "wood"]
            for implementation in order:
                if implementation == "wood":
                    command = [str(args.wood.resolve()), str(args.stdlib.resolve()), str(args.iterations), str(source.resolve())]
                else:
                    command = [str(args.reference.resolve()), "--measure", str(args.iterations), str(source.resolve())]
                started = time.perf_counter_ns()
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                    output, error = process.communicate(timeout=300)
                    wall_ns = time.perf_counter_ns() - started
                    peak = peak_working_set(process)
                    if process.returncode or error:
                        raise RuntimeError((process.returncode, error))
                    measured = json.loads(output)
                current = measured["modules"], measured["symbols"], measured["iterations"]
                if checksum is None:
                    checksum = current
                if current != checksum:
                    raise RuntimeError(f"Frontend checksum mismatch: {source}: {current} != {checksum}")
                case["samples"].append({"implementation": implementation, "sample": sample, "command": command,
                                        "wall_ns": wall_ns, "peak_working_set_bytes": peak, **measured})
        for implementation in ["wood", "cpp"]:
            values = [sample["elapsed_ns"] / args.iterations for sample in case["samples"] if sample["implementation"] == implementation]
            case["summary"][implementation] = {"min_ns_per_graph": min(values), "median_ns_per_graph": statistics.median(values), "max_ns_per_graph": max(values)}
            peaks = [sample["peak_working_set_bytes"] for sample in case["samples"] if sample["implementation"] == implementation and sample["peak_working_set_bytes"] is not None]
            case["summary"][implementation]["max_peak_working_set_bytes"] = max(peaks) if peaks else None
        case["summary"]["wood_cpp_median_ratio"] = case["summary"]["wood"]["median_ns_per_graph"] / case["summary"]["cpp"]["median_ns_per_graph"]
        evidence["cases"].append(case)
        print(json.dumps({"input": str(source), **case["summary"]}), flush=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
