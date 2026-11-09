cmake_minimum_required(VERSION 3.25)
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Linux /proc virtual-memory qualification only")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
set(work "${TEST_DIR}/run-${id}")
file(MAKE_DIRECTORY "${work}")
# A resource bound, not an RSS/throughput benchmark. 200 leaked guarded stacks
# would retain roughly 1.6 GiB. The allowance includes the reference libc's cache.
file(WRITE "${work}/memory.wd" [=[import std.task as tasks
fn identity(message: json) -> json:
    return message
fn pages() -> int:
    return int(split(read_text("/proc/self/statm")," ")[0])
with tasks.spawn(identity,json_null()) as warmup:
    tasks.wait(warmup,10000)
before = pages()
retained: list[task] = []
for i in range(200):
    work = tasks.spawn(identity,json(i))
    assert(tasks.wait(work,10000) == json(i))
    tasks.close(work)
    append(retained,work)
after = pages()
assert(after-before <= 16384,"closed tasks retained guarded worker stacks")
assert(len(retained) == 200)
print(before,after)
]=])
foreach(backend IN ITEMS native cpp)
    set(compiler "${WOOD}")
    set(flags)
    if(backend STREQUAL cpp)
        set(compiler "${PRODUCTION}")
        list(APPEND flags --cxx /usr/bin/g++)
    endif()
    execute_process(COMMAND "${compiler}" run "${work}/memory.wd" -O0 ${flags}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
    file(WRITE "${work}/${backend}.log" "${out}${err}")
    if(NOT status STREQUAL "0" OR NOT err STREQUAL "" OR NOT out MATCHES "^[0-9]+ [0-9]+\n$")
        message(FATAL_ERROR "${backend} closed-task VM bound failed: ${status}: ${out}${err}")
    endif()
endforeach()
file(WRITE "${work}/validation.txt" "Both native and C++ completed 200 sequential workers with closed aliases retained; virtual growth <= 16384 4-KiB pages. This is a guarded-stack release bound, not an RSS benchmark.\n")
message(STATUS "Native closed-task virtual-memory bound passed: ${work}")
