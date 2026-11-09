import sys

requests = int(sys.argv[1])
assert 2 <= requests <= 10_000_000
counts = {}
for i in range(requests):
    key = "key" + str(i % 257)
    counts[key] = counts.get(key, 0) + 1
total = 0
for key in counts:
    total += counts[key]
print(len(counts), total)
