cmake_minimum_required(VERSION 3.25)
file(MAKE_DIRECTORY "${TEST_DIR}")
file(WRITE "${TEST_DIR}/stdin.txt" "0\n1\n2\n3\n4\n5\n6\n7\n")
set(cases native_tasks native_task_ffi native_task_stdin native_task_resources)
if(SANITIZER STREQUAL address)
    list(APPEND cases native_task_limits)
endif()
foreach(case IN LISTS cases)
    set(options)
    if(case STREQUAL native_task_ffi OR case STREQUAL native_task_resources)
        list(APPEND options -DEXTRA_SOURCES=${SOURCE_DIR}/tests/native/task_fixture.c)
    elseif(case STREQUAL native_task_stdin)
        list(APPEND options -DSTDIN_FILE=${TEST_DIR}/stdin.txt)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER}
        -DSTDLIB=${STDLIB} -DSANITIZER=${SANITIZER} -DSOURCE_DIR=${SOURCE_DIR}
        -DINPUT=${SOURCE_DIR}/tests/${case}.wd -DTEST_DIR=${TEST_DIR}/${case} ${options}
        -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 900)
    file(WRITE "${TEST_DIR}/${case}.log" "${out}${err}")
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${SANITIZER} ${case} failed: ${status}: ${out}${err}")
    endif()
endforeach()
foreach(mode IN ITEMS worker ignore parent)
    set(expected 23)
    if(mode STREQUAL ignore)
        set(expected 0)
    elseif(mode STREQUAL parent)
        set(expected 31)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER}
        -DSTDLIB=${STDLIB} -DSANITIZER=${SANITIZER} -DSOURCE_DIR=${SOURCE_DIR}
        -DINPUT=${SOURCE_DIR}/tests/native_task_exit.wd -DTEST_DIR=${TEST_DIR}/exit-${mode}
        -DRUN_ARGS=${mode} -DEXPECTED_EXIT=${expected}
        -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 900)
    file(WRITE "${TEST_DIR}/exit-${mode}.log" "${out}${err}")
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${SANITIZER} task exit ${mode} failed: ${status}: ${out}${err}")
    endif()
endforeach()
file(WRITE "${TEST_DIR}/first.txt" "green red green\n")
file(WRITE "${TEST_DIR}/second.txt" "red blue\n")
file(WRITE "${TEST_DIR}/third.txt" "green\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER}
    -DSTDLIB=${STDLIB} -DSANITIZER=${SANITIZER} -DSOURCE_DIR=${SOURCE_DIR}
    -DINPUT=${SOURCE_DIR}/examples/parallel_word_counts.wd -DTEST_DIR=${TEST_DIR}/file-application
    "-DRUN_ARGS=${TEST_DIR}/first.txt;${TEST_DIR}/second.txt;${TEST_DIR}/third.txt"
    "-DEXPECTED={\"blue\":1,\"green\":3,\"red\":2}\n"
    -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 900)
file(WRITE "${TEST_DIR}/file-application.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "${SANITIZER} concurrent file application failed: ${status}: ${out}${err}")
endif()
message(STATUS "Native ${SANITIZER}/UBSan task lifecycle, callback/stdin isolation, exit cleanup and concurrent file application passed")
