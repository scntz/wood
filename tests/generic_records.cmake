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
    invoke(0 "${SOURCE_DIR}/tests/generic_records.wd" "${level}")
    if(NOT last_output STREQUAL "native generic records and collections passed\n")
        message(FATAL_ERROR "Generic records corpus output: ${last_output}")
    endif()
endforeach()
# Type and method instantiations use the defining package's imports and names.
file(MAKE_DIRECTORY "${TEST_DIR}/lib/src" "${TEST_DIR}/app/src")
file(WRITE "${TEST_DIR}/lib/wood.json" "{\"format\":1,\"name\":\"lib\",\"version\":\"1.0.0\",\"source\":\"src\"}")
file(WRITE "${TEST_DIR}/app/wood.json" "{\"format\":1,\"name\":\"app\",\"version\":\"1.0.0\",\"source\":\"src\",\"entry\":\"main.wd\",\"dependencies\":{\"lib\":{\"path\":\"../lib\",\"version\":\"1.0.0\"}}}")
file(WRITE "${TEST_DIR}/lib/src/values.wd" [=[
record Label:
    value: str
fn decorate(value: str) -> Label:
    return Label("library:" + value)
record Cell[T]:
    value: T
    fn get(self) -> T:
        return self.value
    fn set(mut self, value: T):
        self.value = value
    fn label(self, value: str = "cell") -> Label:
        return decorate(value)
    fn wrap[U](self, value: U) -> Cell[U]:
        return Cell[U](value)
fn unwrap_cell[T](value: Cell[T]) -> T:
    return value.get()
]=])
file(WRITE "${TEST_DIR}/app/src/main.wd" [=[
from lib.values import Cell as Slot, Label, unwrap_cell
import lib.values as values
fn decorate(value: str) -> str:
    return "caller:" + value
record Cell[T]:
    other: T
first = Slot[int](4)
second: values.Cell[str] = values.Cell("text")
first.set(value=5)
assert(unwrap_cell(first) == 5)
assert(first.label().value == "library:cell")
assert(first.wrap((1,"x")).get() == (1,"x"))
assert(second.get() == "text")
assert(first == values.Cell(5))
assert(Cell(1).other == 1)
]=])
invoke(0 lock "${TEST_DIR}/app")
invoke(0 run "${TEST_DIR}/app" --locked)
invoke(0 inspect "${TEST_DIR}/app" --locked)
# The imported module follows the entry module in the module inventory.
string(JSON module_count LENGTH "${last_output}" modules)
set(found FALSE)
math(EXPR module_last "${module_count} - 1")
foreach(module RANGE 0 ${module_last})
    string(JSON export_count LENGTH "${last_output}" modules ${module} exports)
    math(EXPR export_last "${export_count} - 1")
    foreach(export RANGE 0 ${export_last})
        string(JSON name GET "${last_output}" modules ${module} exports ${export} name)
        if(name STREQUAL "Cell")
            string(JSON field_name GET "${last_output}" modules ${module} exports ${export} fields 0 name)
            if(field_name STREQUAL "value")
                string(JSON parameter GET "${last_output}" modules ${module} exports ${export} type_parameters 0)
                string(JSON receiver GET "${last_output}" modules ${module} exports ${export} methods 1 receiver)
                string(JSON argument_kind GET "${last_output}" modules ${module} exports ${export} methods 0 parameters 0 type_ref arguments 0 kind)
                if(NOT parameter STREQUAL "T" OR NOT receiver STREQUAL "mutable" OR NOT argument_kind STREQUAL "type_parameter")
                    message(FATAL_ERROR "Lost generic record/method metadata: ${last_output}")
                endif()
                set(found TRUE)
            endif()
        endif()
    endforeach()
endforeach()
if(NOT found)
    message(FATAL_ERROR "No generic record export in inspection")
endif()
# Same spelling and fields in separate modules never erase nominal identity.
file(APPEND "${TEST_DIR}/app/src/main.wd" "first = Cell(1)\n")
invoke(1 check "${TEST_DIR}/app" --locked)
if(NOT last_error MATCHES "E3002" OR NOT last_error MATCHES "distinct nominal record")
    message(FATAL_ERROR "Nominal generic identities were not checked: ${last_error}")
endif()
if(WIN32)
    set(executable "${TEST_DIR}/route-planner.exe")
else()
    set(executable "${TEST_DIR}/route-planner")
endif()
invoke(0 build "${SOURCE_DIR}/examples/route_planner.wd" -o "${executable}")
include("${SOURCE_DIR}/tests/route_planner_oracle.cmake")
