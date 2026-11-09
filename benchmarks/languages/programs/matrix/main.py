import sys


def fill(n, seed):
    values = [0.0] * (n * n)
    for i in range(n):
        for j in range(n):
            values[i * n + j] = float((i * 7 + j * 3) % 11) * seed
    return values


def run(n):
    left = fill(n, 0.5)
    right = fill(n, 0.25)
    product = [0.0] * (n * n)
    for i in range(n):
        for k in range(n):
            factor = left[i * n + k]
            for j in range(n):
                product[i * n + j] = product[i * n + j] + factor * right[k * n + j]
    trace = 0.0
    for i in range(n):
        trace = trace + product[i * n + i]
    return int(trace)


print(run(int(sys.argv[1])))
