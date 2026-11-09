"""Independent codec/hash checks using Python's standard library as an oracle."""
import hashlib
import json
import random
import subprocess
import sys

probe = sys.argv[1]
rng = random.Random(0x574F4F44)
checks = 0

def run(mode, data):
    result = subprocess.run([probe, mode], input=data, capture_output=True, timeout=10)
    if result.returncode:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))
    return result.stdout.decode("utf-8").strip()

for size in [0, 1, 55, 56, 63, 64, 65, 127, 128, 129, 8191, 8192, 8193, 1000000] + [rng.randrange(20000) for _ in range(100)]:
    data = rng.randbytes(size)
    assert run("sha256", data) == hashlib.sha256(data).hexdigest(), size
    checks += 1

def value(depth=0):
    options = [None, True, False, rng.randrange(-(1 << 63), 1 << 63), rng.uniform(-1e100, 1e100), "".join(rng.choice('abc á🌳\\\"\n\r\t\0') for _ in range(rng.randrange(50)))]
    if depth < 4:
        if rng.randrange(3) == 0:
            return [value(depth + 1) for _ in range(rng.randrange(8))]
        if rng.randrange(3) == 0:
            return {str(i) + "🌳": value(depth + 1) for i in range(rng.randrange(8))}
    return rng.choice(options)

for _ in range(250):
    item = value()
    for ascii_only in [True, False]:
        source = json.dumps(item, ensure_ascii=ascii_only, allow_nan=False).encode()
        assert json.loads(run("json", source)) == item
        checks += 1
print(f"{checks} independent Python differential checks passed")
