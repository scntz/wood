import sys


def run(n):
    values = []
    x = 12345
    for i in range(n):
        x = (x * 1664525 + 1013904223) % 4294967296
        values.append(x)
    stack = [0, n - 1]
    while len(stack) > 0:
        high = stack.pop()
        low = stack.pop()
        if low < high:
            pivot = values[(low + high) // 2]
            i = low
            j = high
            while i <= j:
                while values[i] < pivot:
                    i += 1
                while values[j] > pivot:
                    j -= 1
                if i <= j:
                    values[i], values[j] = values[j], values[i]
                    i += 1
                    j -= 1
            stack.append(low)
            stack.append(j)
            stack.append(i)
            stack.append(high)
    checksum = 0
    for i in range(n):
        checksum = (checksum + values[i] * (i % 7 + 1)) % 1000000007
    return checksum


print(run(int(sys.argv[1])))
