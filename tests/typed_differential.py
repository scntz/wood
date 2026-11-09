"""Optional Python oracle for common tuple, optional, algorithm and call semantics.

Wood type checks and native specializations are exercised by separate tests.
Mutable defaults intentionally retain Wood value semantics and are not equated
with Python's shared mutable default objects.
"""
import json
from pathlib import Path
import random
import subprocess
import sys
import uuid

wood, base = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
work = base / ('run-' + uuid.uuid4().hex)
work.mkdir(parents=True)
binary = work / ('typed-probe.exe' if sys.platform == 'win32' else 'typed-probe')
command = [str(wood), 'build', str(Path(__file__).with_name('typed_probe.wd')),
           '-O2', '-o', str(binary)]
if len(sys.argv) > 3 and sys.argv[3].upper() in ('ON', 'TRUE', '1'):
    command.append('--sanitize=address')
build = subprocess.run(command, capture_output=True, timeout=150)
if build.returncode:
    raise RuntimeError(build.stdout.decode() + build.stderr.decode())

def bind(seed, /, step=2, *, limit=5, tag='d'):
    return seed + step, limit, tag

def pair(left, right):
    return left, right

rng = random.Random(0x54595045)
requests, expected = [], []
for index in range(2400):
    numbers = [rng.randint(-1000, 1000) for _ in range(index % 31)]
    seed, step, limit, mode = rng.randint(-100, 100), rng.randint(-50, 50), rng.randint(0, 10), index % 6
    tag = rng.choice(['', 'label', 'a\0b', '"quoted"'])
    requests.append(dict(numbers=list(numbers), seed=seed, step=step, limit=limit, mode=mode, tag=tag))
    calls = [lambda: bind(seed), lambda: bind(seed, step),
             lambda: bind(seed, tag=tag, limit=limit),
             lambda: bind(seed, tag=tag, step=step, limit=limit),
             lambda: bind(seed, limit=limit, step=step, tag=tag),
             lambda: bind(seed, step, tag=tag)]
    expected.append(dict(bound=calls[mode](), map=[n*n for n in numbers],
                         filter=[n for n in numbers if n % 2 == 0], fold=seed+sum(numbers),
                         first=numbers[0] if numbers else None,
                         find=next((n for n in numbers if n % 2 == 0), None),
                         zip=list(zip(numbers, [str(n) for n in numbers][::-1])),
                         enumerate=list(enumerate(numbers, seed)),
                         partition=([n for n in numbers if n % 2 == 0], [n for n in numbers if n % 2]),
                         optional=seed if mode % 2 == 0 else None,
                         order=pair(right=numbers.pop(), left=numbers.pop()) if len(numbers) >= 2 else ()))
# JSON is the explicit data boundary; tuple identity/repr is tested natively.
expected = json.loads(json.dumps(expected))
payload = ''.join(json.dumps(item) + '\n' for item in requests).encode()
process = subprocess.run([str(binary)], input=payload, capture_output=True, timeout=90, cwd=work)
if process.returncode:
    raise RuntimeError(process.stderr.decode())
actual = [json.loads(line) for line in process.stdout.decode().splitlines()]
if len(actual) != len(expected):
    raise RuntimeError(f'Expected {len(expected)} responses, got {len(actual)}')
for index, (result, reference) in enumerate(zip(actual, expected)):
    if result != reference:
        raise RuntimeError(f'Typed case {index}: {requests[index]!r}\n{result!r} != {reference!r}')

def contract(a, /, b=2, *, c, d=4):
    return a+b+c+d

invalid_calls = ['contract()', 'contract(1)', 'contract(a=1,c=2)',
                 'contract(1,2,3,c=4)', 'contract(1,c=2,b=3,b=4)',
                 'contract(1,2,b=3,c=4)', 'contract(1,c=2,z=3)',
                 'contract(1,c=2,3)', 'contract(1,c=2,c=3)']
for index, call in enumerate(invalid_calls):
    try:
        eval(call)
    except (SyntaxError, TypeError):
        pass
    else:
        raise RuntimeError(f'Python accepted invalid call {call}')
    source = work / f'invalid-{index}.wd'
    source.write_text('def contract(a: int, /, b: int = 2, *, c: int, d: int = 4) -> int:\n'
                      '    return a+b+c+d\n' + call + '\n')
    result = subprocess.run([str(wood), 'check', str(source), '--diagnostics=json'], capture_output=True, timeout=30)
    if result.returncode != 1 or json.loads(result.stderr)['code'] not in ('E2001', 'E3003'):
        raise RuntimeError(f'Unexpected Wood rejection: {call}: {result.stderr!r}')
print(f'{len(expected)} independent Python typed-value/call cases and {len(invalid_calls)} invalid calls passed')
