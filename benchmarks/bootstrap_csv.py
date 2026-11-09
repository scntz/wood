"""Optional native sales-application comparison with independent result validation.

Compare three already-built executables of the unchanged sales_report.wd.
Python is only the development measurement/oracle harness.
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['before', 'after', 'reference']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--samples', type=int, default=7)
    parser.add_argument('--rows', type=int, nargs='+', default=[1000, 2000, 4000])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.samples < 1 or any(n < 1 or n > 10000 for n in args.rows):
        parser.error('positive samples and 1..10000 rows required')
    if args.output.exists():
        parser.error('output already exists')
    scratch = args.output.parent / ('csv-timing-'+uuid.uuid4().hex[:12])
    scratch.mkdir()
    binaries = {name: getattr(args, name).resolve() for name in ['before', 'after', 'reference']}
    report = {'format': 1, 'platform': platform.platform(), 'python_harness': platform.python_version(),
              'scope': 'native process startup, CSV reading/parsing, validation, grouping/comprehensions, JSON output; unchanged application; independent integer/category/list oracle',
              'method': 'one discarded warmup per implementation/case; rotating/reversed order; warm filesystem; no affinity pinning',
              'memory': 'Windows process peak working set, not live heap; allocations not measured in this application harness',
              'samples_per_implementation': args.samples,
              'binaries': {name: {'path': str(path), 'sha256': digest(path)} for name, path in binaries.items()},
              'cases': []}
    for count in args.rows:
        rows = ['category,quantity,unit_cents']
        totals, entries, high = {}, {}, []
        for index in range(count):
            category = ['books', 'food', 'science'][index % 3]
            quantity = 1+index % 7
            price = 20000 if index % 17 == 0 else 125+index % 101
            value = quantity*price
            rows.append(f'{category},{quantity},{price}')
            totals[category] = totals.get(category, 0)+value
            entries[category] = entries.get(category, 0)+1
            if value >= 10000:
                high.append(value)
        expected = {'total_cents': sum(totals.values()), 'categories': [
            {'category': key, 'total_cents': value, 'entries': entries[key]} for key,value in totals.items()],
            'high_value_cents': high}
        source = scratch / f'{count}.csv'
        source.write_text('\n'.join(rows)+'\n', encoding='utf-8')
        case = {'rows': count, 'input': str(source), 'input_bytes': source.stat().st_size,
                'input_sha256': digest(source), 'expected': expected, 'samples': [], 'summary': {}}
        names = list(binaries)
        for sample in range(-1, args.samples):
            offset = sample % len(names)
            order = names[offset:]+names[:offset]
            if sample % 2:
                order.reverse()
            for name in order:
                output = scratch / f'{count}-{sample}-{name}.json'
                command = [str(binaries[name]), str(source), str(output)]
                started = time.perf_counter_ns()
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
                    stdout, stderr = process.communicate(timeout=120)
                    elapsed = time.perf_counter_ns()-started
                    peak = peak_working_set(process)
                    if process.returncode or stdout or stderr:
                        raise RuntimeError((command, process.returncode, stdout, stderr))
                if json.loads(output.read_text(encoding='utf-8')) != expected:
                    raise RuntimeError('Independent sales oracle differs: '+name)
                if sample >= 0:
                    case['samples'].append({'implementation': name, 'sample': sample, 'wall_ns': elapsed,
                                            'peak_working_set_bytes': peak, 'output_sha256': digest(output)})
        for name in names:
            samples = [s for s in case['samples'] if s['implementation'] == name]
            times = [s['wall_ns'] for s in samples]
            peaks = [s['peak_working_set_bytes'] for s in samples if s['peak_working_set_bytes'] is not None]
            case['summary'][name] = {'min_wall_ns': min(times), 'median_wall_ns': statistics.median(times),
                                     'max_wall_ns': max(times), 'max_peak_working_set_bytes': max(peaks) if peaks else None}
        case['after_before_median_ratio'] = case['summary']['after']['median_wall_ns']/case['summary']['before']['median_wall_ns']
        case['after_reference_median_ratio'] = case['summary']['after']['median_wall_ns']/case['summary']['reference']['median_wall_ns']
        report['cases'].append(case)
        print(json.dumps({'rows': count, 'summary': case['summary']}), flush=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
