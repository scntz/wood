from pathlib import Path
import sys

text = Path(sys.argv[1]).read_bytes()
iterations = int(sys.argv[2])
assert 1 <= iterations <= 1_000_000
total = 0
for i in range(iterations):
    total += len(text)
print(total)
