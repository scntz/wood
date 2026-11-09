cmake_minimum_required(VERSION 3.25)
file(MAKE_DIRECTORY "${TEST_DIR}")
set(fixture "${SOURCE_DIR}/tests/fixtures/obj/tetrahedron.obj")
set(output "${TEST_DIR}/sanitized.obj")
set(parent "${TEST_DIR}")
foreach(case IN ITEMS import verify)
    if(case STREQUAL import)
        set(INPUT "${SOURCE_DIR}/examples/mesh_import.wd")
    else()
        set(INPUT "${SOURCE_DIR}/tests/mesh_transform_verify.wd")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG} -DEMITTER=${EMITTER}
        -DSTDLIB=${STDLIB} -DINPUT=${INPUT} -DSOURCE_DIR=${SOURCE_DIR}
        -DTEST_DIR=${parent}/${case} "-DRUN_ARGS=${fixture};${output}"
        -P "${SOURCE_DIR}/tests/native/sanitize.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 600)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Sanitized geometry ${case} failed: ${status}: ${out}${err}")
    endif()
    file(WRITE "${parent}/${case}.log" "${out}${err}")
endforeach()
message(STATUS "Native mesh importing, transforms, export and reimport verified under ASan/UBSan")
