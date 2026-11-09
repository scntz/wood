cmake_minimum_required(VERSION 3.25)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
set(TEST_DIR "${TEST_DIR}/run-${id}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(archive "${TEST_DIR}/fixture with spaces.a")
execute_process(COMMAND "${CLANG}" -std=c17 -ffreestanding -fno-builtin -fno-stack-protector
    -O2 -Wall -Wextra -Werror -c "${SOURCE_DIR}/tests/native/ffi_fixture.c" -o "${TEST_DIR}/fixture.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "FFI fixture compilation failed: ${status}: ${out}${err}")
endif()
execute_process(COMMAND ar rcs "${archive}" "${TEST_DIR}/fixture.o" RESULT_VARIABLE status)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "FFI fixture archive failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
    -DINPUT=${SOURCE_DIR}/tests/native_ffi.wd -DOUTPUT=${TEST_DIR}/program -DTARGET=linux-x86_64
    -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DOPTIMIZATION=${LEVEL}
    -DRUNTIME_OBJECT=${RUNTIME_OBJECT} "-DLINK_LIBRARIES=${archive}"
    -P "${SOURCE_DIR}/tools/build-native.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
file(WRITE "${TEST_DIR}/build.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Native FFI build failed: ${status}: ${out}${err}")
endif()
execute_process(COMMAND readelf -l -d "${TEST_DIR}/program" RESULT_VARIABLE status OUTPUT_VARIABLE deps)
if(NOT status STREQUAL "0" OR deps MATCHES "INTERP|NEEDED")
    message(FATAL_ERROR "Native FFI application is not static: ${deps}")
endif()
execute_process(COMMAND "${TEST_DIR}/program" RESULT_VARIABLE status OUTPUT_VARIABLE native ERROR_VARIABLE err TIMEOUT 60)
file(WRITE "${TEST_DIR}/run.log" "${native}${err}")
if(NOT status STREQUAL "0" OR NOT err STREQUAL "" OR NOT native STREQUAL "native borrowed C ABI passed\n")
    message(FATAL_ERROR "Native FFI failed: ${status}: ${native}${err}")
endif()
execute_process(COMMAND "${PRODUCTION}" "${SOURCE_DIR}/tests/native_ffi.wd" --cxx /usr/bin/g++ -O${LEVEL}
    --link-library "${archive}" RESULT_VARIABLE status OUTPUT_VARIABLE reference ERROR_VARIABLE err TIMEOUT 180)
if(NOT status STREQUAL "0" OR NOT err STREQUAL "" OR NOT native STREQUAL reference)
    message(FATAL_ERROR "Production FFI differs: ${status}: ${reference}${err}")
endif()
# Unresolved C symbols must fail at the linker without publishing an executable.
execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
    -DINPUT=${SOURCE_DIR}/tests/native_ffi_diagnostics.wd -DOUTPUT=${TEST_DIR}/diagnostics -DTARGET=linux-x86_64
    -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DOPTIMIZATION=${LEVEL}
    -DRUNTIME_OBJECT=${RUNTIME_OBJECT} "-DLINK_LIBRARIES=${archive}"
    -P "${SOURCE_DIR}/tools/build-native.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "FFI diagnostic build failed: ${status}: ${out}${err}")
endif()
execute_process(COMMAND "${TEST_DIR}/diagnostics" RESULT_VARIABLE status OUTPUT_VARIABLE native ERROR_VARIABLE err)
if(NOT status STREQUAL "0" OR NOT err STREQUAL "")
    message(FATAL_ERROR "FFI diagnostic execution failed: ${status}: ${native}${err}")
endif()
execute_process(COMMAND "${PRODUCTION}" "${SOURCE_DIR}/tests/native_ffi_diagnostics.wd" --cxx /usr/bin/g++ -O${LEVEL}
    --link-library "${archive}" RESULT_VARIABLE status OUTPUT_VARIABLE reference ERROR_VARIABLE err TIMEOUT 180)
file(WRITE "${TEST_DIR}/diagnostics-native.log" "${native}")
file(WRITE "${TEST_DIR}/diagnostics-production.log" "${reference}${err}")
if(NOT status STREQUAL "0" OR NOT err STREQUAL "" OR NOT native STREQUAL reference)
    message(FATAL_ERROR "FFI error values/source positions differ: ${status}: ${reference}${err}; native: ${native}")
endif()
foreach(case IN ITEMS function_identity native_ffi_exit)
    set(expected 0)
    if(case STREQUAL native_ffi_exit)
        set(expected 23)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DINPUT=${SOURCE_DIR}/tests/${case}.wd -DOUTPUT=${TEST_DIR}/${case} -DTARGET=linux-x86_64
        -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DOPTIMIZATION=${LEVEL}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT} "-DLINK_LIBRARIES=${archive}"
        -P "${SOURCE_DIR}/tools/build-native.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Native ${case} build failed: ${status}: ${out}${err}")
    endif()
    execute_process(COMMAND "${TEST_DIR}/${case}" RESULT_VARIABLE status OUTPUT_VARIABLE native ERROR_VARIABLE err)
    if(NOT status STREQUAL "${expected}" OR NOT err STREQUAL "")
        message(FATAL_ERROR "Native ${case} failed: ${status}: ${native}${err}")
    endif()
    execute_process(COMMAND "${PRODUCTION}" "${SOURCE_DIR}/tests/${case}.wd" --cxx /usr/bin/g++ -O${LEVEL}
        --link-library "${archive}" RESULT_VARIABLE status OUTPUT_VARIABLE reference ERROR_VARIABLE err TIMEOUT 180)
    file(WRITE "${TEST_DIR}/${case}.log" "${native}${reference}${err}")
    if(NOT status STREQUAL "${expected}" OR NOT err STREQUAL "" OR NOT native STREQUAL reference)
        message(FATAL_ERROR "Production ${case} differs: ${status}: ${reference}${err}; native: ${native}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
    -DINPUT=${SOURCE_DIR}/tests/native_ffi.wd -DOUTPUT=${TEST_DIR}/missing -DTARGET=linux-x86_64
    -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DOPTIMIZATION=${LEVEL}
    -DRUNTIME_OBJECT=${RUNTIME_OBJECT} -P "${SOURCE_DIR}/tools/build-native.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
file(WRITE "${TEST_DIR}/missing-library.log" "${out}${err}")
if(status STREQUAL "0" OR EXISTS "${TEST_DIR}/missing" OR NOT err MATCHES "Native linking failed")
    message(FATAL_ERROR "Missing native archive did not fail safely: ${status}: ${out}${err}")
endif()
file(SHA256 "${TEST_DIR}/program" before)
foreach(bad IN ITEMS "${TEST_DIR}/missing.a" "${TEST_DIR}/fixture.o" "${TEST_DIR}" "-lnot-a-path")
    execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DINPUT=${SOURCE_DIR}/tests/native_ffi.wd -DOUTPUT=${TEST_DIR}/rejected -DTARGET=linux-x86_64
        -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DRUNTIME_OBJECT=${RUNTIME_OBJECT}
        "-DLINK_LIBRARIES=${bad}" -P "${SOURCE_DIR}/tools/build-native.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
    if(status STREQUAL "0" OR EXISTS "${TEST_DIR}/rejected" OR NOT err MATCHES "Native link libraries must be existing")
        message(FATAL_ERROR "Invalid archive was not rejected: ${status}: ${out}${err}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
    -DINPUT=${SOURCE_DIR}/tests/native_ffi.wd -DOUTPUT=${TEST_DIR}/program -DTARGET=linux-x86_64
    -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DRUNTIME_OBJECT=${RUNTIME_OBJECT}
    "-DLINK_LIBRARIES=${archive}" -P "${SOURCE_DIR}/tools/build-native.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
file(SHA256 "${TEST_DIR}/program" after)
if(status STREQUAL "0" OR NOT err MATCHES "Native output already exists" OR NOT before STREQUAL after)
    message(FATAL_ERROR "Existing native executable was not preserved")
endif()
file(WRITE "${TEST_DIR}/validation.txt" "O${LEVEL} static ELF, real C archive, exact production differential, missing-library failure without output publication.\n")
message(STATUS "Native FFI and differential checks passed O${LEVEL}: ${TEST_DIR}")
