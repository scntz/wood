import sys


def run(limit):
    composite = [False] * (limit + 1)
    count = 0
    prime = 2
    while prime <= limit:
        if not composite[prime]:
            count += 1
            multiple = prime * prime
            while multiple <= limit:
                composite[multiple] = True
                multiple += prime
        prime += 1
    return count


print(run(int(sys.argv[1])))
