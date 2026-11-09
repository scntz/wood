"""Optional independent Python oracle; generated Wood programs need no Python.

ASCII/NUL strings intentionally compare the common byte/text subset, not Python's
Unicode string model. Extreme signed bounds test clipping without huge allocation.
"""
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import uuid

wood, base = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
work = base / ("run-" + uuid.uuid4().hex)
work.mkdir(parents=True)
binary = work / ("expression-probe.exe" if sys.platform == "win32" else "expression-probe")
command = [str(wood), "build", str(Path(__file__).with_name("expression_probe.wd")),
           "-O2", "-o", str(binary)]
if len(sys.argv) > 3 and sys.argv[3].upper() in ("ON", "TRUE", "1"):
    command.append("--sanitize=address")
build = subprocess.run(command, capture_output=True, timeout=150)
if build.returncode:
    raise RuntimeError(build.stdout.decode("utf-8") + build.stderr.decode("utf-8"))
rng = random.Random(0x45585052)
minimum, maximum = -(1 << 63), (1 << 63) - 1
bounds = [None, minimum, -15, -8, -1, 0, 1, 5, 15, maximum]
steps = [minimum, -maximum, -17, -2, -1, 0, 1, 2, 17, maximum]
vectors = list(itertools.product((0, 1, 2, 5, 9), bounds, bounds, steps))
for _ in range(1500):
    vectors.append((rng.randrange(41), rng.choice(bounds + [rng.randint(minimum, maximum)]),
                    rng.choice(bounds + [rng.randint(minimum, maximum)]),
                    rng.choice(steps + [rng.randint(minimum, maximum)])))
requests, expected = [], []
for size, start, stop, step in vectors:
    numbers = [rng.randrange(-100, 101) for _ in range(size)]
    text = "".join("\0" if i % 5 == 0 else chr(65 + i % 26) for i in range(size))
    repeats = rng.randrange(-2, 4)
    requests.append(dict(numbers=numbers, text=text, start=start, stop=stop,
                         step=step, repeats=repeats))
    if step == 0:
        expected.append({"error": "R4005"})
        continue
    mapping = {str(i % 3): i for i in range(size)}
    expected.append({"list": numbers[start:stop:step], "text": text[start:stop:step],
                     "comprehension": [x * 10 + y for x in range(size) if x % 2 == 0
                                       for y in range(x) if y % 3 == 0],
                     "conditional": size if size % 2 == 0 else -size,
                     "chain": -1 < size < 10, "dictionary": mapping, "keys": list(mapping),
                     "repeat": numbers * repeats + numbers[::-1]})
payload = "".join(json.dumps(item, ensure_ascii=True) + "\n" for item in requests)
process = subprocess.run([str(binary)], input=payload.encode("ascii"), capture_output=True,
                         timeout=90, cwd=work)
if process.returncode:
    raise RuntimeError(process.stderr.decode("utf-8"))
actual = [json.loads(line) for line in process.stdout.decode("utf-8").splitlines()]
if len(actual) != len(expected):
    raise RuntimeError(f"Expected {len(expected)} responses, got {len(actual)}")
for index, (result, reference) in enumerate(zip(actual, expected)):
    if result != reference:
        raise RuntimeError(f"Expression vector {index}: {requests[index]!r}\n{result!r} != {reference!r}")
print(f"{len(expected)} independent Python expression/slice/comprehension cases passed")
