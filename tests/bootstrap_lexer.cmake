cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/frontend-profiles.cmake")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIR "${TEST_DIR}/run-${run_id}/native á 🌳")
file(MAKE_DIRECTORY "${TEST_DIR}")
if(WIN32)
    set(executable "${TEST_DIR}/wood-bootstrap-lex.exe")
else()
    set(executable "${TEST_DIR}/wood-bootstrap-lex")
endif()
set(flags)
if(SANITIZE_GENERATED)
    list(APPEND flags --sanitize=address)
endif()
if(SANITIZE_GENERATED AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    # Check the actual Wood-generated lexer with both sanitizers, not just the
    # C++ oracle. The normal generated-code sanitizer switch is address-only.
    execute_process(COMMAND "${WOOD}" emit-cpp "${SOURCE_DIR}/bootstrap" -o "${TEST_DIR}/lexer.cpp"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Bootstrap C++ emission failed: ${status}\n${error}")
    endif()
    execute_process(COMMAND "${HOST_CXX}" -std=c++23 -O1 -g -fsanitize=address,undefined
        -fno-omit-frame-pointer "${TEST_DIR}/lexer.cpp" -pthread -ldl -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180)
else()
    execute_process(COMMAND "${WOOD}" build "${SOURCE_DIR}/bootstrap" ${flags} -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180)
endif()
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Bootstrap native build failed: ${status}\n${output}\n${error}")
endif()
execute_process(COMMAND "${WOOD}" "${SOURCE_DIR}/bootstrap/tests/test_lexer.wd" ${flags}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 180)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "bootstrap lexer unit checks passed\n")
    message(FATAL_ERROR "Bootstrap unit checks failed: ${status}\n${output}\n${error}")
endif()
execute_process(COMMAND "${ORACLE}" --contract "${SOURCE_DIR}/bootstrap/lexical-contract.json"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Bootstrap lexical metadata does not match the compiler vocabulary: ${error}")
endif()
execute_process(COMMAND "${ORACLE}" --generate "${TEST_DIR}/fixtures"
    RESULT_VARIABLE status OUTPUT_VARIABLE fixture_count ERROR_VARIABLE error TIMEOUT 30)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Fixture generation failed: ${status}\n${error}")
endif()
file(GLOB fixtures "${TEST_DIR}/fixtures/*.wd")
# Source-only roots: never recurse into build trees, caches or junction cycles.
set(corpus)
foreach(directory IN ITEMS stdlib examples tests bootstrap/src bootstrap/tests)
    file(GLOB sources "${SOURCE_DIR}/${directory}/*.wd")
    list(APPEND corpus ${sources})
endforeach()
set(count 0)
set(optional_count 0)
foreach(source IN LISTS fixtures corpus)
    execute_process(COMMAND "${executable}" "${source}" RESULT_VARIABLE actual_status
        OUTPUT_VARIABLE actual ERROR_VARIABLE actual_error TIMEOUT 20)
    execute_process(COMMAND "${ORACLE}" "${source}" RESULT_VARIABLE expected_status
        OUTPUT_VARIABLE expected ERROR_VARIABLE expected_error TIMEOUT 20)
    wood_optional_frontend("${source}" lexer "${actual_status}" "${actual}" "${expected_status}" "${expected}" "${actual_error}" "${expected_error}" optional)
    if(optional)
        math(EXPR optional_count "${optional_count}+1")
        continue()
    endif()
    if(NOT "${actual_status}" STREQUAL "${expected_status}" OR NOT actual STREQUAL expected
       OR NOT actual_error STREQUAL "" OR NOT expected_error STREQUAL "")
        file(WRITE "${TEST_DIR}/actual.json" "${actual}")
        file(WRITE "${TEST_DIR}/expected.json" "${expected}")
        message(FATAL_ERROR "Lexer differential failed: ${source}\nactual status: ${actual_status}; oracle: ${expected_status}\n${actual_error}${expected_error}\nSee ${TEST_DIR}/actual.json and expected.json")
    endif()
    math(EXPR count "${count}+1")
endforeach()
list(LENGTH fixtures generated_count)
list(LENGTH corpus source_count)
execute_process(COMMAND "${executable}" RESULT_VARIABLE usage_status
    OUTPUT_VARIABLE usage ERROR_VARIABLE usage_error TIMEOUT 20)
if(NOT usage_status STREQUAL "2" OR NOT usage MATCHES "^usage: wood-bootstrap-lex"
   OR NOT usage_error STREQUAL "")
    message(FATAL_ERROR "Bootstrap CLI missing-argument contract failed")
endif()
execute_process(COMMAND "${executable}" "${TEST_DIR}/absent.wd" RESULT_VARIABLE missing_status
    OUTPUT_VARIABLE missing_output ERROR_VARIABLE missing_error TIMEOUT 20)
if(NOT missing_status STREQUAL "70" OR NOT missing_error MATCHES "R4016")
    message(FATAL_ERROR "Bootstrap CLI missing-file contract failed: ${missing_status}\n${missing_error}")
endif()
file(WRITE "${TEST_DIR}/bad.wd" "@")
execute_process(COMMAND "${executable}" "${SOURCE_DIR}/bootstrap/src/lexer.wd" "${TEST_DIR}/bad.wd"
    RESULT_VARIABLE actual_status OUTPUT_VARIABLE actual ERROR_VARIABLE actual_error TIMEOUT 20)
execute_process(COMMAND "${ORACLE}" "${SOURCE_DIR}/bootstrap/src/lexer.wd" "${TEST_DIR}/bad.wd"
    RESULT_VARIABLE expected_status OUTPUT_VARIABLE expected ERROR_VARIABLE expected_error TIMEOUT 20)
if(NOT actual_status STREQUAL "1" OR NOT expected_status STREQUAL "1" OR NOT actual STREQUAL expected
   OR NOT actual_error STREQUAL "" OR NOT expected_error STREQUAL "")
    message(FATAL_ERROR "Bootstrap batch JSON Lines/status contract failed")
endif()
message(STATUS "Bootstrap lexer: native unit checks and ${count} exact differential cases passed (${generated_count} generated, ${source_count} repository sources including its own lexer/driver/tests)")
file(WRITE "${TEST_DIR}/validation.txt" "${count} common-profile differential cases passed\n${optional_count} optional-profile acceptance/rejection pairs passed\n${generated_count} generated\n${source_count} source files\n")
