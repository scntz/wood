# wood fmt: the layout of one untidy file, --check, a syntax error, line endings, and every wood file in
# this checkout, which must format without changing its tokens and then need no second pass.
# usage: cmake -DWOOD=wood -DSOURCE_DIR=checkout -DTEST_DIR=new-directory -P tests/formatter.cmake
cmake_minimum_required(VERSION 3.25)
foreach(required WOOD SOURCE_DIR TEST_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Supply ${required}")
    endif()
endforeach()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}")
file(MAKE_DIRECTORY "${TEST_DIR}/one")
function(fmt expected)
    execute_process(COMMAND "${WOOD}" fmt ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE output
        ERROR_VARIABLE error TIMEOUT 600 WORKING_DIRECTORY "${TEST_DIR}")
    if(NOT "${status}" STREQUAL "${expected}")
        message(FATAL_ERROR "wood fmt ${ARGN}: expected ${expected}, got ${status}\n${output}\n${error}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
    set(last_error "${error}" PARENT_SCOPE)
endfunction()

# CMake's file(WRITE) turns \n into \r\n on Windows, so fixtures state their line ending and results
# are compared byte for byte with a file written the same way.
function(write_source path content style)
    file(CONFIGURE OUTPUT "${path}" CONTENT "${content}" @ONLY NEWLINE_STYLE ${style})
endfunction()
function(same_bytes path content style label)
    write_source("${TEST_DIR}/expected.txt" "${content}" ${style})
    file(SHA256 "${path}" actual)
    file(SHA256 "${TEST_DIR}/expected.txt" wanted)
    if(NOT actual STREQUAL wanted)
        file(READ "${path}" text)
        message(FATAL_ERROR "${label}:\n${text}")
    endif()
endfunction()

write_source("${TEST_DIR}/one/untidy.wd" [=[import std.utf8 as utf8
# header comment
fn f(x:int,y:int=2,*,z:str='a')->int:
  # leading comment
  if x>0 and not(y==1):
     return x+-y*2   # trailing
  values=[1,2,
      3,-4]
  table = { "a":1,"b" : [x for x in values if x>1] }
  print(values[1:-1],values[::2],table["a"])
  print(f(1,y=2),len( values ))



  return -x
kinds: dict[str,list[int]|None]={}
g=fn(a:int)->int=>a*2
for k,v in [(1,2)]:
    pass
try:
    raise error("app.x","m")
except err:
    raise
finally:
    print( "done" )
r = Box[list[int]]([1])
]=] UNIX)
set(expected [=[import std.utf8 as utf8
# header comment
fn f(x: int, y: int = 2, *, z: str = 'a') -> int:
    # leading comment
    if x > 0 and not (y == 1):
        return x + -y * 2  # trailing
    values = [1, 2,
        3, -4]
    table = {"a": 1, "b": [x for x in values if x > 1]}
    print(values[1:-1], values[::2], table["a"])
    print(f(1, y=2), len(values))


    return -x
kinds: dict[str, list[int] | None] = {}
g = fn(a: int) -> int => a * 2
for k, v in [(1, 2)]:
    pass
try:
    raise error("app.x", "m")
except err:
    raise
finally:
    print("done")
r = Box[list[int]]([1])
]=])
fmt(1 --check one/untidy.wd)
string(FIND "${last_output}" "would reformat" found)
if(found EQUAL -1)
    message(FATAL_ERROR "fmt --check did not name the file: ${last_output}")
endif()
fmt(0 one/untidy.wd)
same_bytes("${TEST_DIR}/one/untidy.wd" "${expected}" UNIX "Formatted layout differs")
fmt(0 --check one/untidy.wd)
if(NOT last_output STREQUAL "")
    message(FATAL_ERROR "A formatted file needed formatting again: ${last_output}")
endif()

# A file that does not parse is reported and left alone.
write_source("${TEST_DIR}/broken.wd" "x = (1,\nprint(x\n" UNIX)
fmt(1 broken.wd)
if(NOT last_error MATCHES "error\\[E1006\\]")
    message(FATAL_ERROR "A syntax error was not reported: ${last_error}")
endif()
same_bytes("${TEST_DIR}/broken.wd" "x = (1,\nprint(x\n" UNIX "A file with a syntax error changed")

# Windows line endings stay, and hidden, build and vendor directories are not entered.
file(MAKE_DIRECTORY "${TEST_DIR}/lines/.hidden" "${TEST_DIR}/lines/build-debug" "${TEST_DIR}/lines/vendor")
write_source("${TEST_DIR}/lines/crlf.wd" "x=1\nif x>0:\n  print(x)\n" DOS)
foreach(skipped IN ITEMS .hidden build-debug vendor)
    write_source("${TEST_DIR}/lines/${skipped}/skipped.wd" "x=1\n" UNIX)
endforeach()
fmt(0 lines)
same_bytes("${TEST_DIR}/lines/crlf.wd" "x = 1\nif x > 0:\n    print(x)\n" DOS "Line endings were not kept")
foreach(skipped IN ITEMS .hidden build-debug vendor)
    same_bytes("${TEST_DIR}/lines/${skipped}/skipped.wd" "x=1\n" UNIX "fmt entered ${skipped}")
endforeach()

# Every wood file in the checkout: the formatter refuses any result whose tokens differ, so exit 0
# proves no program changed, and a second run must find nothing left to do.
file(GLOB_RECURSE sources RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/stdlib/*.wd" "${SOURCE_DIR}/bootstrap/*.wd"
    "${SOURCE_DIR}/tests/*.wd" "${SOURCE_DIR}/examples/*.wd" "${SOURCE_DIR}/packages/*.wd")
list(LENGTH sources count)
if(count LESS 200)
    message(FATAL_ERROR "Expected the checkout's wood sources, found ${count}")
endif()
foreach(source IN LISTS sources)
    get_filename_component(directory "${TEST_DIR}/tree/${source}" DIRECTORY)
    file(COPY "${SOURCE_DIR}/${source}" DESTINATION "${directory}")
endforeach()
fmt(0 tree)
fmt(0 --check tree)
if(NOT last_output STREQUAL "")
    message(FATAL_ERROR "Formatting the checkout twice changed it again: ${last_output}")
endif()
message(STATUS "wood fmt: layout, --check, errors, line endings and ${count} checkout files passed")
