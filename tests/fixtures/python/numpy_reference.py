"""Independent real-Python oracle for examples/python_numpy.wd (optional only)."""
import json
import numpy as np


def main():
    numbers = np.array([1.0, 2.0, 3.0, 4.0])
    matrix = numbers.reshape((2, 2))
    print(json.dumps(matrix.tolist(), separators=(",", ":")))
    print(float(np.sum(matrix)))
    print(json.dumps(np.matmul(matrix, matrix).tolist(), separators=(",", ":")))
    try:
        numbers.reshape(3)
    except ValueError:
        print("caught Python ValueError")


main()
