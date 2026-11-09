"""Optional development-only semantic fixture throughput measurement.

Build bootstrap/src/semantic_query_main.wd and wood_bootstrap_semantics_oracle.
The C++ executable checks full fixture functions; Wood checks the specified
kernel/preparation boundary. Exact fixture outputs are verified before timing.
This is NOT a full-frontend parity or application-performance benchmark.
"""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path

from bootstrap_modules import peak_working_set


def run(command):
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        output, error = process.communicate(timeout=300)
        peak = peak_working_set(process)
        if process.returncode or error:
            raise RuntimeError((command, process.returncode, error.decode("utf-8", "replace")))
    return output, peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wood", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10000 or not 1 <= args.samples <= 100:
        parser.error("iterations must be 1..10000; samples must be 1..100")
    args.work_dir.mkdir(parents=True, exist_ok=False)
    corpus = args.work_dir / "queries.json"
    run([str(args.reference.resolve()), "--generate", str(corpus.resolve())])
    queries = json.loads(corpus.read_bytes())
    evidence = {
        "format": 1, "platform": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(), "iterations_per_sample": args.iterations,
        "samples_per_implementation": args.samples,
        "binaries": {name: {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                     for name, path in [("wood", args.wood), ("cpp", args.reference)]},
        "corpus_sha256": hashlib.sha256(corpus.read_bytes()).hexdigest(),
        "method": "one warmup per process; native in-process parse/bind/signature/kernel work and result construction/destruction; excludes input JSON decoding, stdout and process startup; alternating implementation order",
        "comparison_limit": "C++ uses the full production body analyzer. Wood uses restricted fixture expression/return kernels or default/structural-flow preparation. Flow queries especially do different work. No full-frontend speedup/parity conclusion is valid.",
        "safety": "actual production compiler diagnostics and exact projected results are checked before measurement; optimized native Wood retains checked runtime arithmetic/indexing and snapshot semantics; no disabled assertions or unchecked implementation",
        "memory": "Windows process peak working set includes runtime, corpus and warmup; not private heap/AST size; unavailable on other hosts",
        "allocations": "not measured", "cases": [],
    }
    for mode in ["expression", "defaults", "returns", "flow"]:
        selected = [query for query in queries if query["mode"] == mode]
        source = args.work_dir / (mode + ".json")
        source.write_text(json.dumps(selected, ensure_ascii=False), encoding="utf-8")
        expected, _ = run([str(args.reference.resolve()), str(source.resolve())])
        actual, _ = run([str(args.wood.resolve()), str(source.resolve())])
        if actual != expected:
            raise RuntimeError("Differential mismatch: " + mode)
        results = [json.loads(line) for line in actual.splitlines()]
        case = {"mode": mode, "queries": len(selected),
                "successes": sum(not result["diagnostic"]["code"] for result in results),
                "failures": sum(bool(result["diagnostic"]["code"]) for result in results),
                "fixture_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "samples": [], "summary": {}}
        checksum = None
        for sample in range(args.samples):
            for implementation in (["wood", "cpp"] if sample % 2 == 0 else ["cpp", "wood"]):
                executable = args.wood if implementation == "wood" else args.reference
                command = [str(executable.resolve()), str(source.resolve()), "--measure", str(args.iterations)]
                output, peak = run(command)
                measured = json.loads(output)
                if measured["queries"] != len(selected) or measured["iterations"] != args.iterations:
                    raise RuntimeError("Incorrect measurement counts")
                if checksum is None:
                    checksum = measured["checksum"]
                if measured["checksum"] != checksum:
                    raise RuntimeError("Incorrect measurement checksum")
                case["samples"].append({"implementation": implementation, "sample": sample,
                                        "command": command, "peak_working_set_bytes": peak, **measured})
        for implementation in ["wood", "cpp"]:
            samples = [sample for sample in case["samples"] if sample["implementation"] == implementation]
            timings = [sample["elapsed_ns"] / args.iterations / 1e6 for sample in samples]
            peaks = [sample["peak_working_set_bytes"] for sample in samples if sample["peak_working_set_bytes"] is not None]
            case["summary"][implementation] = {"median_ms_per_corpus": statistics.median(timings),
                                                "minimum_ms_per_corpus": min(timings),
                                                "maximum_ms_per_corpus": max(timings),
                                                "median_peak_working_set_bytes": statistics.median(peaks) if peaks else None}
        evidence["cases"].append(case)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps([{key: case[key] for key in ["mode", "queries", "successes", "failures", "summary"]}
                      for case in evidence["cases"]], indent=2))


if __name__ == "__main__":
    main()
