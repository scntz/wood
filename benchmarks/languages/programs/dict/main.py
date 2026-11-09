import sys


def run(n, distinct):
    counts = {}
    for i in range(n):
        key = "key" + str(i % distinct)
        counts[key] = counts.get(key, 0) + 1
    total = 0
    for key in counts:
        total += counts[key]
    return total + len(counts)


print(run(int(sys.argv[1]), int(sys.argv[2])))
