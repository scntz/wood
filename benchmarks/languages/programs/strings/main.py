import sys


def run(n, rounds):
    total = 0
    for round in range(rounds):
        parts = []
        for i in range(n):
            parts.append(str(i * 7 + round))
        text = ",".join(parts)
        fields = text.split(",")
        for field in fields:
            total += int(field)
        total += len(text)
    return total


print(run(int(sys.argv[1]), int(sys.argv[2])))
