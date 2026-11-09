cmake_minimum_required(VERSION 3.25)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
set(work "${TEST_DIR}/run-${id}")
file(MAKE_DIRECTORY "${work}/input")
execute_process(COMMAND "${WOOD}" run "${SOURCE_DIR}/tests/native_stdin.wd" -O0 -- generate "${work}/input"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "stdin fixture generation failed: ${out}${err}")
endif()
execute_process(COMMAND "${WOOD}" build "${SOURCE_DIR}/tests/native_stdin.wd" -O0 -o "${work}/plain"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "stdin native build failed: ${out}${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
    -DINPUT=${SOURCE_DIR}/tests/native_stdin.wd -DSOURCE_DIR=${SOURCE_DIR}
    -DSTDIN_FILE=${work}/input/binary -DTEST_DIR=${work}/sanitize
    -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 600)
file(WRITE "${work}/instrumentation.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "stdin instrumentation failed: ${out}${err}")
endif()
file(GLOB instrumented "${work}/sanitize/run-*/program")
file(GLOB binary_log "${work}/sanitize/run-*/run.log")
foreach(case IN ITEMS empty lines crlf binary ctrl-z boundary)
    execute_process(COMMAND "${work}/plain" INPUT_FILE "${work}/input/${case}"
        RESULT_VARIABLE plain_status OUTPUT_VARIABLE plain ERROR_VARIABLE plain_error TIMEOUT 30)
    if(case STREQUAL binary)
        file(READ "${binary_log}" output)
        set(status 0)
        set(err "")
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
            UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "${instrumented}"
            INPUT_FILE "${work}/input/${case}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE err TIMEOUT 60)
    endif()
    file(WRITE "${work}/${case}.log" "${output}${err}")
    if(NOT status STREQUAL "0" OR NOT plain_status STREQUAL "0" OR NOT output STREQUAL plain
        OR NOT err STREQUAL "" OR NOT plain_error STREQUAL "")
        message(FATAL_ERROR "Sanitized stdin ${case} differs: ${status}/${plain_status}: ${err}${plain_error}")
    endif()
endforeach()
execute_process(COMMAND "${CLANG}" -std=c17 "${SOURCE_DIR}/tests/native/stdin_failure.c" -o "${work}/failure"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "stdin failure launcher build failed: ${out}${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "${work}/failure" "${instrumented}" failure
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
file(WRITE "${work}/closed.log" "${out}${err}")
if(NOT status STREQUAL "0" OR NOT out STREQUAL "stdin failure passed\n" OR NOT err STREQUAL "")
    message(FATAL_ERROR "Sanitized closed-stdin error failed: ${status}: ${out}${err}")
endif()
file(WRITE "${work}/validation.txt" "Seven real ASan stdin executions passed: empty, lines, CRLF, binary, Ctrl-Z, boundary and closed descriptor; normal native output equality.\n")
message(STATUS "Native stdin ASan/UBSan passed: ${work}")
