import sys


def run(size, limit):
    inside = 0
    for row in range(size):
        ci = 2.0 * float(row) / float(size) - 1.0
        for column in range(size):
            cr = 2.0 * float(column) / float(size) - 1.5
            zr = 0.0
            zi = 0.0
            step = 0
            while step < limit and zr * zr + zi * zi <= 4.0:
                next_zr = zr * zr - zi * zi + cr
                zi = 2.0 * zr * zi + ci
                zr = next_zr
                step += 1
            if step == limit:
                inside += 1
    return inside


print(run(int(sys.argv[1]), int(sys.argv[2])))
