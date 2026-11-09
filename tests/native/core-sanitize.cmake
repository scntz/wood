cmake_minimum_required(VERSION 3.25)
file(MAKE_DIRECTORY "${TEST_DIR}")
foreach(case IN ITEMS native_spawn_io native_output)
    set(options -DRUN_ARGS=${SOURCE_DIR}/tests/fixtures/tls)
    if(case STREQUAL native_output)
        set(options -DOUTPUT_FIXTURE=${OUTPUT_FIXTURE} -DOUTPUT_MODE=stdout-pipe)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER}
        -DSTDLIB=${STDLIB} -DSANITIZER=${SANITIZER} -DSOURCE_DIR=${SOURCE_DIR}
        -DINPUT=${SOURCE_DIR}/tests/${case}.wd -DTEST_DIR=${TEST_DIR}/${case}
        -DSANITIZER_TIMEOUT=900 -DRESULT_FILE=${TEST_DIR}/${case}-path.txt ${options}
        -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 1200)
    file(MAKE_DIRECTORY "${TEST_DIR}")
    file(WRITE "${TEST_DIR}/${case}.log" "${out}${err}")
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Core ${SANITIZER} ${case}: ${status}: ${out}${err}")
    endif()
endforeach()
file(READ "${TEST_DIR}/native_output-path.txt" run)
foreach(mode IN ITEMS stdout-closed stderr-closed stdout-invalid stderr-invalid stderr-pipe stdout-full stderr-full
    stdout-pipe-masked stderr-pipe-masked stdout-pipe-masked-pending stderr-pipe-masked-pending)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
        TSAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
        "${OUTPUT_FIXTURE}" "${run}/program" "${mode}" "${run}/${mode}.marker"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
    file(WRITE "${run}/${mode}.log" "${out}${err}")
    if(NOT status STREQUAL "0" OR NOT err STREQUAL "")
        message(FATAL_ERROR "Core sanitized output ${mode}: ${status}: ${out}${err}")
    endif()
endforeach()
message(STATUS "Core ${SANITIZER}/UBSan: socket/TLS handoff and twelve real output failure modes passed")
