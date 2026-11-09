cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/frontend-profiles.cmake")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}/native á 🌳")
file(MAKE_DIRECTORY "${TEST_DIR}")
if(WIN32)
    set(executable "${TEST_DIR}/wood-bootstrap-parse.exe")
else()
    set(executable "${TEST_DIR}/wood-bootstrap-parse")
endif()
set(flags)
if(SANITIZE_GENERATED)
    list(APPEND flags --sanitize=address)
endif()
if(SANITIZE_GENERATED AND (HOST_MSVC OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"))
    execute_process(COMMAND "${WOOD}" emit-cpp "${SOURCE_DIR}/bootstrap/src/parse_main.wd"
        -o "${TEST_DIR}/parser.cpp" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 90)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Parser emission failed: ${status}\n${error}")
    endif()
    if(HOST_MSVC)
        # Match wood_compiler's explicit 8 MiB frontend stack: ASan expands
        # recursive frames beyond the Windows executable's 1 MiB default.
        execute_process(COMMAND "${HOST_CXX}" /nologo /std:c++latest /O2 /EHsc /utf-8
            /permissive- /fsanitize=address /Zi "/Fo:${TEST_DIR}/parser.obj"
            "/Fd:${TEST_DIR}/parser.pdb" "/Fe:${executable}" "${TEST_DIR}/parser.cpp"
            /link /INCREMENTAL:NO /STACK:8388608
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 300)
    else()
        execute_process(COMMAND "${HOST_CXX}" -std=c++23 -O1 -g -fsanitize=address,undefined
            -fno-omit-frame-pointer "${TEST_DIR}/parser.cpp" -pthread -ldl -o "${executable}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 300)
    endif()
else()
    execute_process(COMMAND "${WOOD}" build "${SOURCE_DIR}/bootstrap/src/parse_main.wd" ${flags} -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 240)
endif()
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Parser native build failed: ${status}\n${output}\n${error}")
endif()
execute_process(COMMAND "${WOOD}" "${SOURCE_DIR}/bootstrap/tests/test_parser.wd" ${flags}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 240)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "bootstrap parser unit checks passed\n")
    message(FATAL_ERROR "Parser unit checks failed: ${status}\n${output}\n${error}")
endif()
execute_process(COMMAND "${ORACLE}" --generate "${TEST_DIR}/fixtures"
    RESULT_VARIABLE status OUTPUT_VARIABLE generated_count ERROR_VARIABLE error TIMEOUT 180)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Parser fixture generation failed: ${status}\n${error}")
endif()
execute_process(COMMAND "${ORACLE}" --contract "${SOURCE_DIR}/bootstrap/syntactic-contract.json"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Parser metadata does not match the compiler AST vocabulary: ${error}")
endif()
execute_process(COMMAND "${LEXER_ORACLE}" --generate "${TEST_DIR}/lexical"
    RESULT_VARIABLE status OUTPUT_VARIABLE lexical_count ERROR_VARIABLE error TIMEOUT 30)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Lexical fixture generation failed: ${status}\n${error}")
endif()
file(GLOB fixtures "${TEST_DIR}/fixtures/*.wd" "${TEST_DIR}/lexical/*.wd")
set(corpus)
foreach(directory IN ITEMS stdlib examples tests bootstrap/src bootstrap/tests)
    file(GLOB sources "${SOURCE_DIR}/${directory}/*.wd")
    list(APPEND corpus ${sources})
endforeach()
# Bounded project-layout globs include checked-in module/package examples without
# recursively following build trees or junction cycles.
file(GLOB project_sources
    "${SOURCE_DIR}/examples/*/src/*.wd"
    "${SOURCE_DIR}/examples/*/tests/*.wd"
    "${SOURCE_DIR}/examples/packages/*/src/*.wd")
list(APPEND corpus ${project_sources})
set(count 0)
set(optional_count 0)
foreach(source IN LISTS fixtures corpus)
    execute_process(COMMAND "${executable}" "${source}" RESULT_VARIABLE actual_status
        OUTPUT_VARIABLE actual ERROR_VARIABLE actual_error TIMEOUT 30)
    execute_process(COMMAND "${ORACLE}" "${source}" RESULT_VARIABLE expected_status
        OUTPUT_VARIABLE expected ERROR_VARIABLE expected_error TIMEOUT 30)
    wood_optional_frontend("${source}" parser "${actual_status}" "${actual}" "${expected_status}" "${expected}" "${actual_error}" "${expected_error}" optional)
    if(optional)
        math(EXPR optional_count "${optional_count}+1")
        continue()
    endif()
    if(NOT "${actual_status}" STREQUAL "${expected_status}" OR NOT actual STREQUAL expected
       OR NOT actual_error STREQUAL "" OR NOT expected_error STREQUAL "")
        file(WRITE "${TEST_DIR}/actual.json" "${actual}")
        file(WRITE "${TEST_DIR}/expected.json" "${expected}")
        message(FATAL_ERROR "Parser differential failed: ${source}\nactual: ${actual_status}; oracle: ${expected_status}\n${actual_error}${expected_error}\nSee ${TEST_DIR}/actual.json and expected.json")
    endif()
    math(EXPR count "${count}+1")
endforeach()
execute_process(COMMAND "${executable}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT status STREQUAL "2" OR NOT output MATCHES "^usage: wood-bootstrap-parse" OR NOT error STREQUAL "")
    message(FATAL_ERROR "Parser missing-argument contract failed")
endif()
execute_process(COMMAND "${executable}" "${TEST_DIR}/absent.wd" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT status STREQUAL "70" OR NOT error MATCHES "R4016")
    message(FATAL_ERROR "Parser missing-file contract failed: ${status}\n${error}")
endif()
file(WRITE "${TEST_DIR}/bad.wd" "x=\n")
execute_process(COMMAND "${executable}" "${SOURCE_DIR}/bootstrap/src/parser.wd" "${TEST_DIR}/bad.wd"
    RESULT_VARIABLE actual_status OUTPUT_VARIABLE actual ERROR_VARIABLE actual_error TIMEOUT 30)
execute_process(COMMAND "${ORACLE}" "${SOURCE_DIR}/bootstrap/src/parser.wd" "${TEST_DIR}/bad.wd"
    RESULT_VARIABLE expected_status OUTPUT_VARIABLE expected ERROR_VARIABLE expected_error TIMEOUT 30)
if(NOT actual_status STREQUAL "1" OR NOT expected_status STREQUAL "1" OR NOT actual STREQUAL expected
   OR NOT actual_error STREQUAL "" OR NOT expected_error STREQUAL "")
    message(FATAL_ERROR "Parser batch JSON Lines/status contract failed")
endif()
list(LENGTH fixtures fixture_count)
list(LENGTH corpus source_count)
message(STATUS "Bootstrap parser: native unit checks and ${count} exact AST/diagnostic cases passed (${fixture_count} fixtures, ${source_count} repository sources)")
file(WRITE "${TEST_DIR}/validation.txt" "${count} common-profile differential cases passed\n${optional_count} optional-profile acceptance/rejection pairs passed\n${fixture_count} fixtures\n${source_count} source files\n")
