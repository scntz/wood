cmake_minimum_required(VERSION 3.25)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
if(NOT DEFINED SANITIZER)
    set(SANITIZER address)
endif()
if(NOT SANITIZER MATCHES "^(address|thread)$")
    message(FATAL_ERROR "SANITIZER must be address or thread")
endif()
set(TEST_DIR "${TEST_DIR}/run-${id}")
file(MAKE_DIRECTORY "${TEST_DIR}")
execute_process(COMMAND "${CLANG}" -std=c17 -O1 -g -ffp-contract=off
    -fsanitize=${SANITIZER},undefined -fno-omit-frame-pointer -DWOOD_NATIVE_HOSTED_THREADS=1 -DWD_HEAP_BYPASS=1
    "${SOURCE_DIR}/runtime/native/managed.c" "${SOURCE_DIR}/tests/native/managed_runtime.c"
    "${SOURCE_DIR}/tests/native/thread_host.c" -pthread -lm -o "${TEST_DIR}/primitives" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
file(WRITE "${TEST_DIR}/build.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Primitive sanitizer build failed: ${status}: ${out}${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    TSAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "${TEST_DIR}/primitives"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
file(WRITE "${TEST_DIR}/run.log" "${out}${err}")
if(NOT status STREQUAL "0" OR NOT err STREQUAL "")
    message(FATAL_ERROR "Primitive sanitizer execution failed: ${status}: ${out}${err}")
endif()
message(STATUS "Native primitive ownership, OOM/file faults and math oracle passed: ${out}")
