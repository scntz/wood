# Python packages

The native compiler can call into CPython 3.14 from a wood program. This is experimental and opt-in: the compiler, its
bootstrap and ordinary programs never need Python, and a wood installation contains none. It runs real CPython in your
process; it isn't a Python implementation. Only the LLVM backend supports it, not the C++ reference compiler.

## The managed path

```sh
wood add python@numpy
wood run
```

[`wood add python@...`](packages.md#python-packages) downloads a pinned CPython 3.14, creates
an environment for the project and installs wheels pinned by hash, without needing or touching
a system Python. `wood run` and `wood test` set `WOOD_PYTHON` to that environment. If you set
`WOOD_PYTHON` yourself, your choice wins. If the lock names Python packages and the environment
is missing, they stop with **D5108**: run `wood install`.

## Shipping an application

```sh
wood build --bundle-python -o dist
```

This writes a directory with the executable, a private `wood-python/` copy of the runtime and
the locked packages, and `PYTHON-NOTICES.txt`. The executable finds `wood-python/` beside itself,
so the directory runs on a machine without Python, and it can be moved as a whole. Bundling only
covers the platform you build on.

`PYTHON-NOTICES.txt` lists the CPython version and every package with its version and license
metadata. You remain responsible for complying with those licenses when you redistribute them.
The bundle has been demonstrated on Debian 13 with no Python installed and `PATH` emptied. On
Windows 11, with Python installed elsewhere on the machine, `tools/test-python-bundle.ps1` inspects what the
running process has loaded: every Python-related module came from the bundle or from Windows, and nothing came
from anywhere else. It is not code signed, and antivirus or installer integration is yours to handle.

A bundle ignores `PYTHONHOME`, `PYTHONPATH` and `VIRTUAL_ENV`, and it carries no `pip`. If it is damaged, or the
interpreter is missing, the application reports an error and exits with status 70 rather than crashing. A
program that needs Python but has no bundle reports R4030 and says what to do. A wrong `WOOD_PYTHON` is an
error, not a reason to try another interpreter.

An application that imports no Python module is unaffected by all of this: it contains no Python
and needs none.

The rest of this page describes the bridge itself and how to select an interpreter by hand.

## Requirements and interpreter selection

Use a native installation, its matching ABI-4 runtime, and a trusted **64-bit, GIL-enabled CPython 3.14** installation with a
shared library. Other Python minor versions, free-threaded builds, subinterpreters
and embedded-host interpreters are explicitly unsupported in this initial profile.
The bridge uses the opaque initialization API introduced in CPython 3.14; it does
not guess `PyConfig` or `PyObject` layouts. No Python headers or import library are
needed to compile a wood application.

At the first Python operation, selection is:

1. `WOOD_PYTHON`: an absolute path to the desired Python executable.
2. `wood-python/` beside the running executable, as `wood build --bundle-python` writes it.
3. `VIRTUAL_ENV`: its `Scripts/python.exe` on Windows or `bin/python3` on Linux.
4. `python.exe` / `python3` in absolute PATH directories. Empty and relative PATH
   entries and the current directory are excluded.

An explicit selection that fails is an error; wood never substitutes another
interpreter. wood starts the selected executable once with `-I` and a fixed probe,
without a shell, with a 15-second deadline and 64-KiB output limit. It discovers
the selected environment's isolated search paths, prefixes, version, GIL mode and
absolute shared-library path. The main interpreter then uses explicit search
paths, environment/user-site isolation, disabled signal-handler and C-stdio
configuration, disabled bytecode writing and disabled repeated site initialization.
The library and Python extension dependencies must already be loadable.

The probe executes the selected installation's trusted site initialization,
including its applicable `.pth` files. Interoperability is **trusted native code
execution**, not a sandbox or a package-security boundary. Only select interpreters,
virtual environments, packages and PATH directories you trust. `PYTHONPATH` and
`PYTHONHOME` are not supported configuration mechanisms. Configure the selected
virtual environment instead. The bridge itself does not install or download packages.
When you select an interpreter by hand, wood locks do not pin it or its packages, so review
and manage those separately. Updating the standard-module inventory may require a deliberate
`wood lock` refresh; locked verification never refreshes itself automatically.

For example, create a separate virtual environment using an installed Python 3.14:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install numpy
$env:WOOD_PYTHON = (Resolve-Path .venv\Scripts\python.exe).Path
wood run examples/python_numpy.wd --backend=llvm
```

```sh
python3.14 -m venv .venv
.venv/bin/python -m pip install numpy
export WOOD_PYTHON="$PWD/.venv/bin/python"
wood run examples/python_numpy.wd --backend=llvm
```

These commands assume the selected `python` is the required version and that the
example is available locally. Use an absolute interpreter path if PATH differs.
Windows uses the selected interpreter's base `python314.dll` and restricted DLL
search flags; Linux loads its selected shared libpython with global symbol
visibility, which extension modules need. On Linux, a program that uses Python starts through glibc's loader, like programs
that use SQLite or TLS. Other programs don't.

## Imports, calls and attributes

`import python@numpy as np` creates a wood namespace. `import python@math` uses
`math` as its default name; dotted packages and explicit aliases work too.
`from python@... import ...` is deliberately unsupported. Existing filesystem and
`std.*` imports retain their meaning, including ordinary local modules named
`python`. Loading/checking/inspecting a Python namespace does not execute Python;
its `module()`, `attr(name)` and `call(name, arguments, keywords)` functions do.

The namespace has explicit typed operations rather than speculative Python type
inference. `np.sum(values)` is not implemented; use `np.call("sum", ...)`.
For a complete runnable example, see [python_numpy.wd](../examples/python_numpy.wd).
The following is its basic calling pattern:

```text
import python@numpy as np
import std.python as py

with np.call("array", [py.value([1.0, 2.0, 3.0])]) as array:
    print(py.to_float(np.call("sum", [array])))
    print(py.to_json(py.call(py.attr(array, "tolist"))))
```

`py.attr(object, name)` retrieves a module attribute, object property or bound
method as a `py_object`. `py.call(object, arguments=[], keywords={})` invokes any
Python callable, including returned functions/classes and bound methods. Keyword
values and positional values are explicitly boxed objects. Python validates its
own signatures. Attribute access and conversion may execute Python code. For
attribute assignment, Python's real `builtins.setattr` is callable through the
same interface. There are no generated/manual C bindings or fabricated stubs.

Module/attribute names must be NUL-free UTF-8; data strings and bytes retain
embedded NUL. Python/library arithmetic follows that library's own numeric rules;
it does not change native wood's checked arithmetic. `sys.argv` is not an automatic
mirror of wood's `args()`; pass command arguments explicitly to Python APIs.

## Conversion rules

| wood operation | Python/native result and limits |
| --- | --- |
| `py.value(value)` | Copies JSON-compatible native data to Python. Int64 -> Python int, finite float -> float, bool -> bool, valid UTF-8 bytes -> Unicode, None/absent optional -> None, list -> list, tuple -> list, string-key dict/record -> dict. Uses existing `json(value)` validation. |
| `py.tuple(list[py_object])` | Creates a real Python tuple, retaining each supplied object. |
| `py.bytes(str)` / `py.to_bytes(object)` | Explicit binary copy; preserves NUL and invalid UTF-8. Extraction requires Python bytes. |
| `py.to_json(object)` | Copies exact builtin None/bool/int/float/str/list/tuple/string-key dict to immutable native JSON; tuples become JSON lists. Checked int64 range, finite floats, strict UTF-8, at most 64 nested levels and one million visited nodes. Cycles, non-string keys and library/custom objects are rejected. |
| `py.to_int(object)` | Python integer/index protocol; checked signed 64-bit range. Does not silently truncate float. |
| `py.to_float(object)` | Python numeric float conversion. Native binary64 permits nonfinite results; JSON conversion does not. |
| `py.to_bool(object)` | Explicit Python truth testing; does not change wood's bool-only condition semantics. |
| `py.text(object)` | Explicit Python `str(object)` to UTF-8 native bytes; may execute user code. |

`py_object` is a statically typed, opaque managed identity, not a COW native value.
Use explicit conversions at the boundary. NumPy scalars can use numeric extraction;
arrays should use `tolist()` before `to_json`. Unsupported automatic conversion
fails instead of guessing. Native arrays can first use `std.num.to_list` and
`std.num.shape`, then construct/reshape a NumPy array. This copies deliberately:
NumPy may retain mutable views, while wood arrays require pinned COW snapshots.
No zero-copy buffer protocol, dtype/layout inference or array borrowing is claimed.

## Ownership, tasks and shutdown

Each wrapper owns one strong Python reference. wood copies retain the native
wrapper; they share Python identity. Borrowed Python references are copied or
retained before escaping; argument tuples obey CPython's stealing-reference rules.
Native list/dict/array values preserve their existing COW behavior.

`with object as resource:` closes the originally entered identity on normal exit,
return, break and checked failure. `py.close(object)` invalidates all wrapper
aliases and is idempotent. `py.closed(object)` tests that state. Closing publishes
the closed state before decref, because a Python destructor can execute arbitrary
code. Unclosed wrappers automatically release their Python references on final
native release. Printing a wrapper gives `<py_object>`; it does not implicitly run
Python formatting. Comparing wrappers is rejected; convert values explicitly.

Close releases the wrapper's reference; it does **not** call Python `close()`,
`__enter__` or `__exit__`. Explicitly invoke a Python resource's own cleanup API
when needed. Separately acquired wrappers may refer to the same Python object
while owning independent references; closing one invalidates its wood copies,
not every independently acquired reference or the Python module cache.

Initialize Python on the main wood thread before spawning Python-using tasks.
Each operation acquires/releases the GIL. Workers own their own wrappers and
exchange native JSON using the existing task API; Python handles cannot be sent
as JSON payloads. Worker-first initialization fails catchably. Close/join tasks
before shutdown; existing scoped joining and worker error forwarding remain in
force. Cancellation is cooperative at call boundaries; arbitrary Python/C extension
code cannot be forcibly interrupted safely. A blocked Python function can still
block a join. wood callbacks from Python and transferring wrappers between external
threads are not supported.

The generated application's final cleanup releases native values/tasks before
finalizing Python on its initialization thread. `py.shutdown()` optionally performs
that step earlier, but refuses live objects, active operations or worker-thread
shutdown. Explicitly closed wrappers may remain. Initialization failure or shutdown
ends the interpreter session: switching interpreters or reinitializing extension
modules in the same process is unsupported. The shared library remains mapped
until process exit for extension destructors. Python's own finalizers and threads
have CPython's normal shutdown behavior; wood cannot make untrusted finalizers safe.

## Errors and troubleshooting

Python failures are ordinary catchable wood errors with the existing five error
fields and source-site reporting. The original error is cleared from CPython before
returning to wood. Exception class and message are reported (bounded to 1 KiB and
8 KiB); traceback objects are not exposed. A broken exception `__str__` receives a
safe fallback. `SystemExit` is a Python exception at this boundary, not a request
to terminate wood.

- **R4030**: missing/unsupported interpreter, failed initialization or ended session.
  Select an absolute GIL-enabled CPython 3.14 executable with its shared library.
- **R4031**: Python import/call/conversion exception or shutdown failure. The message
  includes the Python exception type when available. Install a missing package in
  the selected environment, rather than changing wood's native module search path.
- **R4032**: closed object, unsupported/cyclic/oversized conversion or unsafe explicit
  shutdown. Convert library values explicitly and close/join resources first.
- Existing native errors remain applicable: for example invalid UTF-8 or nonfinite
  `json(value)` fails with R4012 before calling Python; task cancellation uses R4022.
- The self-hosted C++ driver rejects this profile with B3501; old production
  compilers also do not recognize the provider grammar. Use the matching native CLI.

## Testing and limits

The `native_python` test runs only when you give it `PYTHON_EXECUTABLE` and `NUMPY_EXECUTABLE`; no other test needs
Python. It builds real programs at `-O0` and `-O2`, and checks conversions, exceptions, reference counts, NumPy in a virtual
environment, a missing interpreter, tasks and shutdown, and that nothing falls back to C++. A sanitizer variant instruments
wood's code and its C bridge, though not CPython or NumPy themselves.

One problem is open: at shutdown, NumPy leaves 586,823 bytes in 483 allocations under LeakSanitizer. A plain C program
using CPython's API directly, without wood, leaks exactly the same, so wood keeps the interpreter alive until the program
exits. Until that's resolved, NumPy interop isn't considered leak-free.

You can run the same test directly, with absolute paths:

```sh
cmake -DWOOD=/path/to/native/bin/wood -DSOURCE_DIR=/path/to/wood-source \
  -DTEST_DIR=/path/to/new-test-evidence \
  -DPYTHON_EXECUTABLE=/path/to/python3.14 \
  -DNUMPY_EXECUTABLE=/path/to/numpy-venv/bin/python3 \
  -P /path/to/wood-source/tests/native/python.cmake
```

On Windows, use `python.exe` and `Scripts/python.exe`. The test uses the installed `wood`'s LLVM and linker settings, installs
nothing, and keeps everything it creates under `TEST_DIR`.

Only CPython 3.14 with the GIL is supported. There are no zero-copy arrays, subinterpreters or free-threaded Python. Python
and NumPy are never needed to build or bootstrap wood.

Implementation references: [CPython initialization](https://docs.python.org/3.14/c-api/init_config.html),
[reference counting](https://docs.python.org/3.14/c-api/refcounting.html),
[thread state and GIL](https://docs.python.org/3.14/c-api/init.html).
