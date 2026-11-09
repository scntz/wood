"""Independent interoperability checks against Python's standard csv module."""
import csv
import io
import json
from pathlib import Path
import random
import subprocess
import sys

wood, test_dir = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
test_dir.mkdir(parents=True, exist_ok=True)
binary = test_dir / ("csv-probe.exe" if sys.platform == "win32" else "csv-probe")
source = Path(__file__).with_name("csv_probe.wd")
subprocess.run([str(wood), "build", str(source), "-o", str(binary)], check=True,
               capture_output=True, timeout=120)
requests, expected = [], []
rng = random.Random(0x574F4F44)
alphabet = 'abc012 ,;\t"\r\n\0á😀'
for delimiter in (",", ";", "\t"):
    cases = [[], [[]], [[""]], [["", ""]], [["a", "b"]],
             [["quote\"here", "two\nlines", "\r\n", "á😀", "\0"]]]
    for _ in range(100):
        rows = [["".join(rng.choice(alphabet) for _ in range(rng.randrange(24)))
                 for _ in range(rng.randrange(6))] for _ in range(rng.randrange(8))]
        cases.append(rows)
    for rows in cases:
        stream = io.StringIO(newline="")
        csv.writer(stream, delimiter=delimiter, lineterminator="\r\n").writerows(rows)
        text = stream.getvalue()
        parsed = list(csv.reader(io.StringIO(text, newline=""), delimiter=delimiter, strict=True))
        if parsed != rows:
            raise RuntimeError("Python reference writer/reader disagreed")
        requests.append({"delimiter": delimiter, "text": text})
        expected.append({"rows": rows, "encoded": text})
payload = "".join(json.dumps(item, ensure_ascii=False) + "\n" for item in requests)
process = subprocess.run([str(binary)], input=payload.encode("utf-8"), capture_output=True,
                         check=True, timeout=120)
actual = [json.loads(line) for line in process.stdout.decode("utf-8").splitlines()]
if len(actual) != len(expected):
    raise RuntimeError(f"Expected {len(expected)} responses, got {len(actual)}")
for index, (result, reference) in enumerate(zip(actual, expected)):
    if result != reference:
        raise RuntimeError(f"CSV case {index}: {result!r} != {reference!r}")
print(f"{len(expected)} independent Python CSV cases passed (parse and write)")
