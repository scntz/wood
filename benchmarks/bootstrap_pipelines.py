"""Optional native hosted/production/checked-C++ comparison on existing workloads.

All binaries are built separately with the same optimized host toolchain. Python
only orchestrates measurements and checks independent integer checksums.
"""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path

from bootstrap_modules import peak_working_set


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['hosted', 'production', 'reference', 'previous']:
        parser.add_argument('--'+name, type=Path, required=name != 'previous')
    parser.add_argument('--count', type=int, default=1000000)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--samples', type=int, default=7)
    parser.add_argument('--instrumented', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.samples < 1 or not 4 <= args.count <= 10000000 or not 1 <= args.rounds <= 100:
        parser.error('positive samples, count 4..10000000 and rounds 1..100 required')
    if args.output.exists():
        parser.error('output already exists')
    binaries = {name: getattr(args, name).resolve() for name in ['hosted', 'production', 'reference', 'previous'] if getattr(args, name)}
    report = {'format': 1, 'platform': platform.platform(), 'python_harness': platform.python_version(),
              'method': 'one discarded warmup; seven samples by default; rotating/reversed order; warm filesystem; no affinity pinning',
              'scope': 'existing benchmarks/lazy_pipelines.wd vs production-generated native code and existing reference_lazy_pipelines.cpp in checked mode; before/after hosted callee borrowing if previous supplied',
              'kernel_timer': 'program monotonic nanoseconds; excludes process startup and JSON output',
              'memory': 'process peak working set includes startup; instrumented global-new requests cover complete application entry, not only the timed kernel; no allocator bookkeeping/direct C allocations',
              'instrumented': args.instrumented, 'count': args.count, 'rounds': args.rounds,
              'samples_per_implementation': args.samples,
              'binaries': {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for name, path in binaries.items()},
              'cases': []}
    accepted = args.count//4
    pipeline = sum(3*accepted*(accepted-1)//2*(r % 7+1)+accepted for r in range(args.rounds))
    blocks, rest = divmod(args.count, 256)
    capture = args.rounds*(args.count*1408+blocks*32640+rest*(rest-1)//2)
    for workload, expected in [('pipeline', pipeline), ('capture', capture)]:
        case = {'workload': workload, 'expected_checksum': expected, 'samples': [], 'summary': {}}
        names = list(binaries)
        for sample in range(-1, args.samples):
            offset = sample % len(names)
            order = names[offset:]+names[:offset]
            if sample % 2:
                order.reverse()
            for name in order:
                started = time.perf_counter_ns()
                with subprocess.Popen([str(binaries[name]), workload, str(args.count), str(args.rounds), 'checked'], stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                    stdout, stderr = process.communicate(timeout=120)
                    wall = time.perf_counter_ns()-started
                    peak = peak_working_set(process)
                    if process.returncode or (stderr and not args.instrumented):
                        raise RuntimeError((name, workload, process.returncode, stdout, stderr))
                    data = json.loads(stdout)
                    if data['checksum'] != expected or data['elapsed_ns'] <= 0:
                        raise RuntimeError('Independent checksum/timer differs: '+name+'/'+workload)
                    metrics = json.loads(stderr) if args.instrumented else None
                    if metrics is not None and any(not isinstance(metrics.get(key), int) or metrics[key] < 0 for key in ['allocations','allocated_bytes','peak_bytes','elapsed_ns']):
                        raise RuntimeError('Invalid allocation instrumentation')
                if sample >= 0:
                    row = {'implementation': name, 'sample': sample, 'wall_ns': wall, 'kernel_ns': data['elapsed_ns'], 'peak_working_set_bytes': peak, 'checksum': data['checksum']}
                    if metrics is not None:
                        row['instrumentation'] = metrics
                    case['samples'].append(row)
        for name in names:
            rows = [row for row in case['samples'] if row['implementation'] == name]
            summary = {}
            for key in ['wall_ns','kernel_ns']:
                values = [row[key] for row in rows]
                summary['min_'+key], summary['median_'+key], summary['max_'+key] = min(values), statistics.median(values), max(values)
            peaks = [row['peak_working_set_bytes'] for row in rows if row['peak_working_set_bytes'] is not None]
            summary['max_peak_working_set_bytes'] = max(peaks) if peaks else None
            if args.instrumented:
                for key in ['allocations','allocated_bytes','peak_bytes']:
                    values = [row['instrumentation'][key] for row in rows]
                    summary['min_'+key], summary['max_'+key] = min(values), max(values)
            case['summary'][name] = summary
        report['cases'].append(case)
        print(json.dumps({'workload': workload, 'summary': case['summary']}), flush=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
