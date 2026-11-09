import sys

limit = int(sys.argv[1])
assert 2 <= limit <= 10_000_000
composite = []
for _ in range(limit + 1):
    composite.append(False)
count = 0
prime = 2
while prime <= limit:
    if not composite[prime]:
        count += 1
        if prime <= limit // prime:
            multiple = prime * prime
            while multiple <= limit:
                composite[multiple] = True
                multiple += prime
    prime += 1
print(count)
