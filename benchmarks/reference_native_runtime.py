"""Independent Python output oracle. No clocks or allocation-ratio claims.

Python is optional benchmark tooling, never a compiler/runtime dependency.
The bounded fixtures avoid integer overflow and use ASCII byte-equivalent text.
"""
import csv
import io
import json
import pathlib
import sys


def main():
    if len(sys.argv) != 5:
        raise ValueError("expected mode, count, rounds, output path")
    mode, size, repeats, output = sys.argv[1:]
    count, rounds = int(size), int(repeats)
    if not 1 <= count <= 10000 or not 1 <= rounds <= 100:
        raise ValueError("unsupported profile size")
    if mode not in {"list", "snapshot", "scalars", "map", "pipeline", "csv", "array"}:
        raise ValueError("unknown profile mode")
    fields = [f"member-{i}:" + "payload-" * 12 for i in range(count)]
    rows = [[fields[(i + j) % count] for j in range(16)] for i in range(count)]
    numeric = [float(i % 17) for i in range(count)]
    result, scalar, array_result, text, checksum = [], [], [], "", 0
    for _ in range(rounds):
        if mode in {"list", "snapshot"}:
            values = []
            for field in fields:
                values.append(field)
            if mode == "snapshot":
                snapshot = values.copy()  # Explicit value isolation, unlike Python assignment.
                values[-1] = "changed"
                assert snapshot[-1] == fields[-1]
            checksum += sum(len(value) for value in values)
            result = values
        elif mode == "scalars":
            scalar = []
            for value in range(count):
                scalar.append(value)
            checksum += sum(scalar)
        elif mode == "map":
            table = {}
            for index in range(count):
                table[index] = fields[index]
            checksum += sum(len(table[index]) for index in range(count))
            result = [table[index] for index in range(count)]
        elif mode == "pipeline":
            checksum += sum(map(len, fields))
            result = fields
        elif mode == "csv":
            buffer = io.StringIO(newline="")
            csv.writer(buffer, lineterminator="\r\n").writerows(rows)
            text = buffer.getvalue()
            checksum += len(text)
        else:
            array_result = [value * 2.0 + 1.0 for value in numeric]
            checksum += int(sum(array_result))
    if mode == "scalars":
        text = "\n".join(str(value) for value in scalar)
    elif mode == "array":
        text = "\n".join(str(int(value)) for value in array_result)
    elif mode != "csv":
        text = "\n".join(result)
    pathlib.Path(output).write_bytes(text.encode("ascii"))
    print(json.dumps(dict(checksum=checksum, allocations=-1, requested_bytes=-1,
                         peak_additional_bytes=-1, retains=-1, releases=-1,
                         unique_managed_slots=-1, shared_managed_slots=-1)))


if __name__ == "__main__":
    main()
