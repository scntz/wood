string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}")
file(MAKE_DIRECTORY "${TEST_DIR}")
function(invoke expected)
    set(flags)
    if(SANITIZE_GENERATED AND (ARGV1 STREQUAL "build" OR ARGV1 STREQUAL "run" OR ARGV1 MATCHES "[.]wd$"))
        list(APPEND flags --sanitize=address)
    endif()
    execute_process(COMMAND "${WOOD}" ${ARGN} ${flags} RESULT_VARIABLE status
        OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180 ENCODING UTF-8 WORKING_DIRECTORY "${TEST_DIR}")
    if(NOT "${status}" STREQUAL "${expected}")
        message(FATAL_ERROR "wood ${ARGN}: expected ${expected}, got ${status}\n${output}\n${error}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
    set(last_error "${error}" PARENT_SCOPE)
endfunction()
foreach(level IN ITEMS -O0 -O2)
    invoke(0 "${SOURCE_DIR}/tests/native_computing.wd" "${level}")
    if(NOT last_output STREQUAL "native computing passed\n")
        message(FATAL_ERROR "Unexpected native computing output ${last_output}")
    endif()
endforeach()
file(WRITE "${TEST_DIR}/ffi.wd" [=[
import std.num as num
extern fn wood_array_sum(values: array[float]) -> float
extern fn wood_array_scale(mut values: array[float], factor: float)
extern fn wood_array_copy(mut output: array[float], input: array[float])
values = num.from_list([1.0,2.0,3.0])
snapshot = values
wood_array_scale(values,2.0)
assert(wood_array_sum(values) == 12.0 and wood_array_sum(snapshot) == 6.0)
wood_array_copy(values,snapshot)
assert(values == snapshot)
wood_array_copy(values,values)
assert(wood_array_sum(values) == 6.0)
wood_array_scale(values[1:3],2.0)
]=])
# A slice is a value, so mutable foreign calls require a rooted variable.
invoke(1 check "${TEST_DIR}/ffi.wd")
file(READ "${TEST_DIR}/ffi.wd" ffi_source)
string(REPLACE "wood_array_scale(values[1:3],2.0)" [=[
view = values[::-1]
try:
    wood_array_scale(view,2.0)
    assert(false)
except err:
    assert(err.code == "R4026")
materialized = num.copy(view)
wood_array_scale(materialized,2.0)
assert(num.to_list(materialized) == [6.0,4.0,2.0])
record Pool:
    buffers: list[array[float]]
    fn replace(mut self):
        self.buffers = [num.full(3,7.0)]
        return num.full(3,2.0)
pool = Pool([num.ones(3)])
alias = pool
wood_array_copy(pool.buffers[0], pool.replace())
assert(num.to_list(pool.buffers[0]) == [2.0,2.0,2.0])
assert(num.to_list(alias.buffers[0]) == [1.0,1.0,1.0])
]=] ffi_source "${ffi_source}")
file(WRITE "${TEST_DIR}/ffi.wd" "${ffi_source}")
invoke(0 "${TEST_DIR}/ffi.wd" --link-library "${FFI_LIBRARY}")
if(WIN32)
    set(executable "${TEST_DIR}/mesh.exe")
else()
    set(executable "${TEST_DIR}/mesh")
endif()
invoke(0 build "${SOURCE_DIR}/examples/mesh_torus.wd" -o "${executable}")
foreach(subdivision IN ITEMS "16,12" "48,32")
    string(REPLACE "," ";" parts "${subdivision}")
    list(GET parts 0 rings)
    list(GET parts 1 sides)
    execute_process(COMMAND "${executable}" "${TEST_DIR}/mesh.obj" "${rings}" "${sides}"
        RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error TIMEOUT 60)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Native mesh application failed: ${error}")
    endif()
    file(WRITE "${TEST_DIR}/report.json" "${report}")
    execute_process(COMMAND "${OBJ_CHECK}" "${TEST_DIR}/mesh.obj" "${TEST_DIR}/report.json" "${rings}" "${sides}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Independent OBJ validation failed: ${output}\n${error}")
    endif()
    message(STATUS "${output}")
endforeach()
# Geometry validation must occur before opening/truncating output.
file(WRITE "${TEST_DIR}/invalid.wd" [=[
import std.geometry as geo
import std.num as num
write_text("preserve.obj","preserve")
try:
    geo.write_obj(geo.Mesh(num.zeros((1,3)),num.ints((1,3))),"preserve.obj")
    assert(false)
except err:
    assert(read_text("preserve.obj") == "preserve")
]=])
invoke(0 "${TEST_DIR}/invalid.wd")
# Numeric array types, generic records and borrowing modes cross locked packages.
file(MAKE_DIRECTORY "${TEST_DIR}/lib/src" "${TEST_DIR}/app/src")
file(WRITE "${TEST_DIR}/lib/wood.json" "{\"format\":1,\"name\":\"lib\",\"version\":\"1.0.0\",\"source\":\"src\"}")
file(WRITE "${TEST_DIR}/app/wood.json" "{\"format\":1,\"name\":\"app\",\"version\":\"1.0.0\",\"source\":\"src\",\"entry\":\"main.wd\",\"dependencies\":{\"lib\":{\"path\":\"../lib\",\"version\":\"1.0.0\"}}}")
file(WRITE "${TEST_DIR}/lib/src/buffers.wd" [=[
import std.num as num
extern fn wood_array_scale(mut values: array[float], factor: float)
record Buffer[T]:
    values: array[T]
    fn reversed(self):
        return Buffer(self.values[::-1])
fn make():
    return Buffer(num.from_list([1.0,2.0,3.0]))
fn scaled(values: array[float], factor: float):
    wood_array_scale(values,factor)
    return values
]=])
file(WRITE "${TEST_DIR}/app/src/main.wd" [=[
import lib.buffers as buffers
import std.num as num
original = buffers.make()
reversed = original.reversed()
reversed.values[0] = 9.0
assert(num.to_list(original.values) == [1.0,2.0,3.0])
assert(num.to_list(reversed.values) == [9.0,2.0,1.0])
result = buffers.scaled(original.values,2.0)
assert(num.to_list(result) == [2.0,4.0,6.0])
assert(num.to_list(original.values) == [1.0,2.0,3.0])
]=])
invoke(0 lock "${TEST_DIR}/app")
invoke(0 run "${TEST_DIR}/app" --locked --link-library "${FFI_LIBRARY}")
invoke(0 inspect "${TEST_DIR}/app" --locked)
if(NOT last_output MATCHES "\"kind\":\"array\"" OR NOT last_output MATCHES "\"mutable_borrow\":true")
    message(FATAL_ERROR "Missing numeric array/borrowing metadata: ${last_output}")
endif()
# An ABI cannot disagree about const/mutable access across imported modules.
file(WRITE "${TEST_DIR}/readonly.wd" "extern fn conflicting_buffer(values: array[float])\n")
file(WRITE "${TEST_DIR}/writable.wd" "extern fn conflicting_buffer(mut values: array[float])\n")
file(WRITE "${TEST_DIR}/conflict.wd" "import readonly as reader\nimport writable as writer\n")
invoke(1 check "${TEST_DIR}/conflict.wd")
if(NOT last_error MATCHES "E3023")
    message(FATAL_ERROR "Expected cross-module ABI conflict: ${last_error}")
endif()
