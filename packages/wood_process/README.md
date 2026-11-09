# wood_process 0.1.0

Start other programs and find your application's own files. Listed in the catalog as
`wood@process`. Pure wood over the native runtime's process support, on Windows and Linux.

```sh
wood add wood@process
```

```wood
import process.process as process

result = process.run(["git", "--version"], 5000)
print(result.status)
print(result.output)

logo = process.resource("assets/logo.png")
```

| Function | Purpose |
| --- | --- |
| `run(arguments, timeout_ms, output_limit)` | Run to completion; returns `status` and combined `output` |
| `checked(arguments, timeout_ms)` | Like `run`, raising an error on a nonzero status |
| `set_environment(name, value)` | Set a variable for programs started afterwards |
| `executable()` | This program's own path |
| `resource(relative)` | A file beside the executable; `..` is rejected |

There is no shell, so each argument reaches the program exactly as given. On Windows, a batch
file raises an error, since `cmd.exe` would parse its arguments, and the current directory is not
searched for a program named without a path. On a timeout or
when output exceeds its limit, the whole process tree is stopped and an error is raised. A
program that cannot be started also raises an error.

Status: experimental, native LLVM backend only. It runs programs to completion and gives
no streaming, stdin or background handles. To wait without blocking, call it from a
`std.task` worker. On Linux, standard error and output arrive merged, as on Windows.

Licensed MIT OR Apache-2.0.
