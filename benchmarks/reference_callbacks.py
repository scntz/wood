import sys


def square_plus(value):
    return value * value + 1


def square_minus(value):
    return value * value - 1


count = int(sys.argv[1])
callback = square_plus
if count < 0:
    callback = square_minus
    count = -count
total = 0
for i in range(count):
    total += callback(i % 256)
print(total)
