string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}")
file(MAKE_DIRECTORY "${TEST_DIR}")
function(invoke expected)
    set(flags)
    if(SANITIZE_GENERATED AND (ARGV1 STREQUAL "build" OR ARGV1 STREQUAL "run" OR ARGV1 MATCHES "[.]wd$"))
        list(APPEND flags --sanitize=address)
    endif()
    execute_process(COMMAND "${WOOD}" ${ARGN} ${flags} RESULT_VARIABLE status
        OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180 ENCODING UTF-8
        WORKING_DIRECTORY "${TEST_DIR}")
    if(NOT "${status}" STREQUAL "${expected}")
        message(FATAL_ERROR "wood ${ARGN}: expected ${expected}, got ${status}\n${output}\n${error}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
    set(last_error "${error}" PARENT_SCOPE)
endfunction()
foreach(level IN ITEMS -O0 -O2)
    invoke(0 "${SOURCE_DIR}/tests/closures_iterators.wd" "${level}")
    if(NOT last_output STREQUAL "native closures and lazy iterators passed\n")
        message(FATAL_ERROR "Unexpected closures/iterators output: ${last_output}")
    endif()
endforeach()
# Definition-module closure captures survive package boundaries and shadowing.
file(MAKE_DIRECTORY "${TEST_DIR}/lib/src" "${TEST_DIR}/app/src")
file(WRITE "${TEST_DIR}/lib/wood.json" "{\"format\":1,\"name\":\"lib\",\"version\":\"1.0.0\",\"source\":\"src\"}")
file(WRITE "${TEST_DIR}/app/wood.json" "{\"format\":1,\"name\":\"app\",\"version\":\"1.0.0\",\"source\":\"src\",\"entry\":\"main.wd\",\"dependencies\":{\"lib\":{\"path\":\"../lib\",\"version\":\"1.0.0\"}}}")
file(WRITE "${TEST_DIR}/lib/src/values.wd" [=[
import std.iter as it
record Label:
    value: str
fn prefix(value: str) -> str:
    return "library:" + value
fn factory[T](value: T) -> callable() -> T:
    return fn() => value
fn labels[S](source: S) -> list[Label]:
    return it.collect(it.map(source, fn(value) => Label(prefix(str(value)))))
fn apply[S,F](source: S, callback: F(S.Item)) -> list[F.Result]:
    return it.collect(it.map(source, callback))
]=])
file(WRITE "${TEST_DIR}/app/src/main.wd" [=[
import lib.values as lib
fn prefix(value: str) -> str:
    return "caller:" + value
assert(lib.factory((1,"x"))() == (1,"x"))
assert(lib.labels(range(3))[1].value == "library:1")
offset = 4
assert(lib.apply([1,2], fn(value) => value + offset) == [5,6])
]=])
invoke(0 lock "${TEST_DIR}/app")
invoke(0 run "${TEST_DIR}/app" --locked)
invoke(0 inspect "${TEST_DIR}/app" --locked)
if(NOT last_output MATCHES "callable_pattern" OR NOT last_output MATCHES "associated_type" OR NOT last_output MATCHES "callable")
    message(FATAL_ERROR "Lost callable/associated type metadata: ${last_output}")
endif()
if(WIN32)
    set(executable "${TEST_DIR}/log-report.exe")
else()
    set(executable "${TEST_DIR}/log-report")
endif()
invoke(0 build "${SOURCE_DIR}/examples/log_report.wd" -o "${executable}")
include("${SOURCE_DIR}/tests/log_report_oracle.cmake")
# Stream cursor aliases close safely, and blank lines are not mistaken for EOF.
# with-scoped bindings do not escape; retain an alias explicitly instead.
file(WRITE "${TEST_DIR}/lines.wd" [=[
import std.io as io
import std.iter as it
write_text("lines.txt", "a\n\nb")
handle = io.open("lines.txt", "r")
cursor = it.lines(handle)
read_captured = fn() => io.line(handle, 64)
with handle as source:
    assert(it.collect(cursor) == ["a\n","\n","b"])
try:
    next(cursor)
    assert(false)
except err:
    assert(err.code == "R4016")
try:
    read_captured()
    assert(false)
except err:
    assert(err.code == "R4016")
]=])
invoke(0 "${TEST_DIR}/lines.wd")
foreach(source IN ITEMS
    "import std.iter as it\nx = it.from_fn(fn() => 1)\n"
    "import std.iter as it\nx = it.collect(it.map([1], fn(x) => print(x)))\n"
    "import std.iter as it\nx = it.collect(it.chain([1], ['a']))\n")
    file(WRITE "${TEST_DIR}/invalid.wd" "${source}")
    invoke(1 check "${TEST_DIR}/invalid.wd")
    if(NOT last_error MATCHES "E3002")
        message(FATAL_ERROR "Expected a type error: ${last_error}")
    endif()
endforeach()
