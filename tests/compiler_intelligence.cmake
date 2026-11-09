string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}")
file(MAKE_DIRECTORY "${TEST_DIR}")
function(invoke expected)
    set(flags)
    if(SANITIZE_GENERATED AND (ARGV1 STREQUAL "run" OR ARGV1 MATCHES "[.]wd$"))
        list(APPEND flags --sanitize=address)
    endif()
    execute_process(COMMAND "${WOOD}" ${ARGN} ${flags} RESULT_VARIABLE status
        OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180 ENCODING UTF-8
        WORKING_DIRECTORY "${TEST_DIR}")
    if(NOT "${status}" STREQUAL "${expected}")
        message(FATAL_ERROR "wood ${ARGN}: expected ${expected}, got ${status}\n${output}\n${error}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()
foreach(level IN ITEMS -O0 -O2)
    invoke(0 "${SOURCE_DIR}/tests/compiler_intelligence.wd" "${level}")
    if(NOT last_output STREQUAL "compiler intelligence passed\n")
        message(FATAL_ERROR "Unexpected inference output: ${last_output}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${TEST_DIR}/lib/src" "${TEST_DIR}/app/src")
file(WRITE "${TEST_DIR}/lib/wood.json" "{\"format\":1,\"name\":\"lib\",\"version\":\"1.0.0\",\"source\":\"src\"}")
file(WRITE "${TEST_DIR}/app/wood.json" "{\"format\":1,\"name\":\"app\",\"version\":\"1.0.0\",\"source\":\"src\",\"entry\":\"main.wd\",\"dependencies\":{\"lib\":{\"path\":\"../lib\",\"version\":\"1.0.0\"}}}")
file(WRITE "${TEST_DIR}/lib/src/api.wd" [=[
import std.iter as it
fn prefix(value: str):
    return "library:" + value
fn factory[T](value: T):
    return fn() => value
record Box[T]:
    value: T
    fn read(self):
        return prefix(str(self.value))
fn labels[S](source: S):
    return it.collect(it.map(source, fn(value) => prefix(str(value))))
]=])
file(WRITE "${TEST_DIR}/app/src/main.wd" [=[
import lib.api as lib
fn prefix(value: str):
    return "caller:" + value
assert(lib.factory((1,"x"))() == (1,"x"))
assert(lib.Box(3).read() == "library:3")
assert(lib.labels(range(3)) == ["library:0","library:1","library:2"])
]=])
invoke(0 lock "${TEST_DIR}/app")
invoke(0 run "${TEST_DIR}/app" --locked)
invoke(0 inspect "${TEST_DIR}/app" --locked)
if(NOT last_output MATCHES "\"return_inferred\":true" OR NOT last_output MATCHES "\"return_resolved\":true" OR NOT last_output MATCHES "\"return_resolved\":false")
    message(FATAL_ERROR "Missing inferred return metadata: ${last_output}")
endif()
