cmake_minimum_required(VERSION 3.25)
project(wood_native_validation LANGUAGES NONE)
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
enable_testing()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND DEFINED CLANG AND DEFINED EMITTER)
    add_test(NAME native_random_provider_sanitize COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/random-provider
        -P "${CMAKE_CURRENT_LIST_DIR}/random-provider.cmake")
    set_tests_properties(native_random_provider_sanitize PROPERTIES TIMEOUT 180)
    add_test(NAME native_windows_path_size COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/wide-size
        -P "${CMAKE_CURRENT_LIST_DIR}/wide-size.cmake")
    set_tests_properties(native_windows_path_size PROPERTIES TIMEOUT 180)
    foreach(sanitizer IN ITEMS address thread)
        foreach(fixture IN ITEMS random task_checkpoint)
            add_test(NAME native_${fixture}_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
                -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
                -DINPUT=${SOURCE_DIR}/tests/${fixture}.wd -DSANITIZER=${sanitizer}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${fixture}-${sanitizer}
                -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
            set_tests_properties(native_${fixture}_${sanitizer}_sanitize PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        endforeach()
        add_test(NAME native_environment_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
            -DINPUT=${CMAKE_CURRENT_LIST_DIR}/../environment.wd
            -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DSANITIZER=${sanitizer}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/environment-${sanitizer}
            "-DENVIRONMENT_VARIABLES=WOOD_STDLIB_EMPTY=;WOOD_STDLIB_VALUE=hé🌳=value;WOOD_STDLIB_🌳=unicode-key;--unset=WOOD_STDLIB_ABSENT_53c69ef0"
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_environment_${sanitizer}_sanitize PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    endforeach()
endif()
if(DEFINED INSTALLED_CLI AND DEFINED PYTHON_EXECUTABLE AND DEFINED NUMPY_EXECUTABLE)
    add_test(NAME native_python COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/python
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE} -DNUMPY_EXECUTABLE=${NUMPY_EXECUTABLE}
        -P "${CMAKE_CURRENT_LIST_DIR}/python.cmake")
    set_tests_properties(native_python PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND DEFINED CLANG)
        add_test(NAME native_python_address_sanitize COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
            -DINPUT=${CMAKE_CURRENT_LIST_DIR}/../native_python.wd
            -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/python-address-sanitize
            -DSANITIZER_TIMEOUT=900 -DENVIRONMENT_VARIABLES=WOOD_PYTHON=${PYTHON_EXECUTABLE}
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_python_address_sanitize PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
    endif()
endif()
add_test(NAME native_catalog COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
    -P "${CMAKE_CURRENT_LIST_DIR}/../../tools/check-native-coverage.cmake")
if(DEFINED LEGACY_RUNTIME_OBJECT)
    if(WIN32)
        set(legacy_target windows-x86_64)
    else()
        set(legacy_target linux-x86_64)
    endif()
    add_test(NAME native_abi4_legacy COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLEGACY_RUNTIME_OBJECT=${LEGACY_RUNTIME_OBJECT}
        -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DTARGET=${legacy_target}
        -DWSL_DISTRIBUTION=${WSL_DISTRIBUTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/abi4-legacy
        -P "${CMAKE_CURRENT_LIST_DIR}/abi4-compat.cmake")
    set_tests_properties(native_abi4_legacy PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
endif()
if(DEFINED INSTALLED_CLI)
    add_test(NAME native_official_packages COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/official-packages
        -P "${CMAKE_CURRENT_LIST_DIR}/../official_packages.cmake")
    set_tests_properties(native_official_packages PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    add_test(NAME native_desktop_presentation COMMAND "${INSTALLED_CLI}" test
        "${SOURCE_DIR}/examples/desktop_notes" --locked --report=json)
    set_tests_properties(native_desktop_presentation PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native_method_lowering_fault COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/method-lowering-fault
        -P "${CMAKE_CURRENT_LIST_DIR}/method-lowering-fault.cmake")
    set_tests_properties(native_method_lowering_fault PROPERTIES TIMEOUT 3900 RUN_SERIAL TRUE)
    add_test(NAME native_package COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-package
        -P "${CMAKE_CURRENT_LIST_DIR}/package.cmake")
    set_tests_properties(native_package PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    if(DEFINED PRODUCTION)
        add_test(NAME native_stdlib COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
            -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
            "-DCPP_RUNTIME=${CPP_RUNTIME}"
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib -P "${CMAKE_CURRENT_LIST_DIR}/../stdlib.cmake")
        set_tests_properties(native_stdlib PROPERTIES TIMEOUT 7200 RUN_SERIAL TRUE)
        add_test(NAME native_sequence_ownership COMMAND "${CMAKE_COMMAND}"
            -DWOOD=${INSTALLED_CLI} -DPRODUCTION=${PRODUCTION}
            -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sequence-ownership
            -P "${CMAKE_CURRENT_LIST_DIR}/sequence-ownership.cmake")
        set_tests_properties(native_sequence_ownership PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        if(DEFINED BACKEND_TEST)
            if(DEFINED OUTPUT_FIXTURE)
                add_test(NAME native_core_lifecycle COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                    -DPRODUCTION=${PRODUCTION} -DBACKEND_TEST=${BACKEND_TEST} -DOUTPUT_FIXTURE=${OUTPUT_FIXTURE} -DPWSH=${PWSH}
                    -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/core-lifecycle
                    -P "${CMAKE_CURRENT_LIST_DIR}/core-lifecycle.cmake")
                set_tests_properties(native_core_lifecycle PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
            endif()
            add_test(NAME native_tls COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                -DPRODUCTION=${PRODUCTION} -DBACKEND_TEST=${BACKEND_TEST} -DPWSH=${PWSH}
                -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/tls
                -P "${CMAKE_CURRENT_LIST_DIR}/tls.cmake")
            set_tests_properties(native_tls PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
            add_test(NAME native_tls_ipv6 COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                -DPRODUCTION=${PRODUCTION} -DBACKEND_TEST=${BACKEND_TEST} -DTLS_IPV6_ONLY=ON
                -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/tls-ipv6
                -P "${CMAKE_CURRENT_LIST_DIR}/tls.cmake")
            set_tests_properties(native_tls_ipv6 PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        endif()
        if(DEFINED NETWORK_TEST)
            add_test(NAME native_network COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                -DPRODUCTION=${PRODUCTION} -DNETWORK_TEST=${NETWORK_TEST} -DCLANG=${CLANG}
                -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/network
                -P "${CMAKE_CURRENT_LIST_DIR}/network.cmake")
            set_tests_properties(native_network PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
        endif()
        add_test(NAME native_sqlite COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
            -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sqlite
            -P "${CMAKE_CURRENT_LIST_DIR}/sqlite.cmake")
        set_tests_properties(native_sqlite PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
        add_test(NAME native_runtime_parity COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
            -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/runtime-parity
            -P "${CMAKE_CURRENT_LIST_DIR}/runtime-parity.cmake")
        set_tests_properties(native_runtime_parity PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
        if(NOT WIN32 OR DEFINED TASK_LIBRARY)
            set(task_options)
            if(DEFINED TASK_LIBRARY)
                list(APPEND task_options -DTASK_LIBRARY=${TASK_LIBRARY})
            endif()
            add_test(NAME native_tasks COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/tasks ${task_options}
                -P "${CMAKE_CURRENT_LIST_DIR}/tasks.cmake")
            set_tests_properties(native_tasks PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
            if(NOT WIN32)
                add_test(NAME native_task_memory COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                    -DPRODUCTION=${PRODUCTION} -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/task-memory
                    -P "${CMAKE_CURRENT_LIST_DIR}/task-memory.cmake")
                set_tests_properties(native_task_memory PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
            endif()
        endif()
        foreach(case IN ITEMS runtime-diagnostics stdin-pipe)
            string(REPLACE "-" "_" test_name "${case}")
            add_test(NAME native_${test_name} COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
                -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${case}
                -P "${CMAKE_CURRENT_LIST_DIR}/${case}.cmake")
            set_tests_properties(native_${test_name} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        endforeach()
    endif()
    add_test(NAME native_cli COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli
        -P "${CMAKE_CURRENT_LIST_DIR}/cli.cmake")
    set_tests_properties(native_cli PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
    add_test(NAME native_cli_bootstrap COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-bootstrap
        -P "${CMAKE_CURRENT_LIST_DIR}/cli-bootstrap.cmake")
    set_tests_properties(native_cli_bootstrap PROPERTIES TIMEOUT 7200 RUN_SERIAL TRUE)
    add_test(NAME native_cli_install COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
        -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.. -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-install
        -P "${CMAKE_CURRENT_LIST_DIR}/cli-install.cmake")
    set_tests_properties(native_cli_install PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
endif()
if(WIN32 AND DEFINED INSTALLED_CLI)
    return() # Normal CLI gates are portable; PE ABI gates remain in windows.ps1.
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Use windows.ps1 for the Windows native gate; this CTest project verifies Linux ELF")
endif()
foreach(required IN ITEMS EMITTER STDLIB LLC OPT LINKER PRODUCTION RUNTIME_OBJECT)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Supply existing -D${required}=<absolute path>")
    endif()
endforeach()
foreach(level IN ITEMS 0 2)
    add_test(NAME native_callables_O${level} COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLLC=${LLC} -DOPT=${OPT}
        -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/callables-O${level} -DLEVEL=${level}
        -P "${CMAKE_CURRENT_LIST_DIR}/callables.cmake")
    set_tests_properties(native_callables_O${level} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
endforeach()
if(DEFINED CLANG)
    add_test(NAME native_allocation_profile_oracle COMMAND "${CMAKE_COMMAND}"
        -DCLANG=${CLANG} -DSOURCE_DIR=${SOURCE_DIR}
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/profile-oracle
        -P "${SOURCE_DIR}/benchmarks/check_allocation_sites.cmake")
    set_tests_properties(native_allocation_profile_oracle PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native_sanitize_sequence_ownership COMMAND "${CMAKE_COMMAND}"
        -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DSOURCE_DIR=${SOURCE_DIR} -DINPUT=${SOURCE_DIR}/tests/native_sequence_ownership.wd
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-sequence-ownership
        -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
    set_tests_properties(native_sanitize_sequence_ownership PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_test(NAME native_primitive_thread_sanitize COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
        -DSOURCE_DIR=${SOURCE_DIR} -DSANITIZER=thread
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/thread-primitives
        -P "${CMAKE_CURRENT_LIST_DIR}/primitives.cmake")
    set_tests_properties(native_primitive_thread_sanitize PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    foreach(sanitizer IN ITEMS address thread)
        if(DEFINED OUTPUT_FIXTURE)
            add_test(NAME native_core_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
                -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
                -DOUTPUT_FIXTURE=${OUTPUT_FIXTURE} -DSANITIZER=${sanitizer}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/core-${sanitizer}-sanitize
                -P "${CMAKE_CURRENT_LIST_DIR}/core-sanitize.cmake")
            set_tests_properties(native_core_${sanitizer}_sanitize PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
        endif()
        add_test(NAME native_tls_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
            -DINPUT=${SOURCE_DIR}/tests/native_tls.wd -DSANITIZER=${sanitizer}
            -DRUN_ARGS=${SOURCE_DIR}/tests/fixtures/tls -DSANITIZER_TIMEOUT=900
            -DENVIRONMENT_VARIABLES=SSL_CERT_FILE=${SOURCE_DIR}/tests/fixtures/tls/ca.pem
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/tls-${sanitizer}-sanitize
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_tls_${sanitizer}_sanitize PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        add_test(NAME native_tasks_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
            -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR} -DSANITIZER=${sanitizer}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/tasks-${sanitizer}-sanitize
            -P "${CMAKE_CURRENT_LIST_DIR}/tasks-sanitize.cmake")
        set_tests_properties(native_tasks_${sanitizer}_sanitize PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
    endforeach()
    if(DEFINED INSTALLED_CLI)
        add_test(NAME native_sanitize_stdin COMMAND "${CMAKE_COMMAND}" -DWOOD=${INSTALLED_CLI}
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-stdin
            -P "${CMAKE_CURRENT_LIST_DIR}/stdin-sanitize.cmake")
        set_tests_properties(native_sanitize_stdin PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
    endif()
    if(DEFINED DRIVER_PROCESS_FIXTURE)
        add_test(NAME native_sanitize_driver_host COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
            -DINPUT=${SOURCE_DIR}/bootstrap/tests/native_driver_host.wd -DSOURCE_DIR=${SOURCE_DIR}
            -DRUN_ARGS=${DRIVER_PROCESS_FIXTURE} -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-driver-host
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_sanitize_driver_host PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    endif()
    if(DEFINED INSTALLED_CLI)
        get_filename_component(native_bin "${INSTALLED_CLI}" DIRECTORY)
        get_filename_component(native_prefix "${native_bin}" DIRECTORY)
        add_test(NAME native_sanitize_normal_driver COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
            -DINPUT=${SOURCE_DIR}/bootstrap/src/driver_main.wd -DSOURCE_DIR=${SOURCE_DIR}
            -DINSTALL_DATA=${native_prefix}/share/wood -DSANITIZER_TIMEOUT=1800 -DIR_OPTIMIZATION=0
            "-DRUN_ARGS=run;${SOURCE_DIR}/examples/mesh_import.wd;-O0;--;${SOURCE_DIR}/tests/fixtures/obj/tetrahedron.obj;${CMAKE_CURRENT_BINARY_DIR}/sanitized-mesh.obj"
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-normal-driver
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_sanitize_normal_driver PROPERTIES TIMEOUT 5400 RUN_SERIAL TRUE)
    endif()
    add_test(NAME native_primitive_sanitize COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
        -DSOURCE_DIR=${SOURCE_DIR} -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-primitives
        -P "${CMAKE_CURRENT_LIST_DIR}/primitives.cmake")
    set_tests_properties(native_primitive_sanitize PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    foreach(level IN ITEMS 0 2)
        add_test(NAME native_ffi_O${level} COMMAND "${CMAKE_COMMAND}"
            -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLLC=${LLC} -DOPT=${OPT}
            -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
            -DRUNTIME_OBJECT=${RUNTIME_OBJECT} -DCLANG=${CLANG} -DLEVEL=${level}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/ffi-O${level}
            -P "${CMAKE_CURRENT_LIST_DIR}/ffi.cmake")
        set_tests_properties(native_ffi_O${level} PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native_sanitize_ffi COMMAND "${CMAKE_COMMAND}"
        -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DINPUT=${SOURCE_DIR}/tests/native_ffi.wd -DSOURCE_DIR=${SOURCE_DIR}
        -DEXTRA_SOURCES=${SOURCE_DIR}/tests/native/ffi_fixture.c
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-ffi
        -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
    set_tests_properties(native_sanitize_ffi PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_test(NAME native_sanitize_ffi_exit COMMAND "${CMAKE_COMMAND}"
        -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DINPUT=${SOURCE_DIR}/tests/native_ffi_exit.wd -DSOURCE_DIR=${SOURCE_DIR}
        -DEXTRA_SOURCES=${SOURCE_DIR}/tests/native/ffi_fixture.c -DEXPECTED_EXIT=23
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-ffi-exit
        -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
    set_tests_properties(native_sanitize_ffi_exit PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    foreach(case IN ITEMS time native_runtime_values native_self_update native_format_resources native_callables native_iteration native_stream_lines closures_iterators binding_lifetimes functional native_arrays native_array_errors native_array_diagnostics native_text native_computing stdlib_contracts stdlib_numerical_edges)
        add_test(NAME native_sanitize_${case} COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
            -DINPUT=${SOURCE_DIR}/tests/${case}.wd -DSOURCE_DIR=${SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-${case}
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_sanitize_${case} PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    endforeach()
    foreach(sanitizer IN ITEMS address thread)
        add_test(NAME native_network_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
            -DINPUT=${SOURCE_DIR}/tests/native_network.wd -DSANITIZER=${sanitizer}
            -DSANITIZER_TIMEOUT=900 -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/network-${sanitizer}-sanitize
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_network_${sanitizer}_sanitize PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        if(DEFINED NETWORK_TEST)
            add_test(NAME native_network_pressure_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
                -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
                -DINPUT=${SOURCE_DIR}/tests/native_network_pressure.wd -DSANITIZER=${sanitizer}
                -DEXTERNAL_RUNNER=${NETWORK_TEST} -DSANITIZER_TIMEOUT=900
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/network-pressure-${sanitizer}-sanitize
                -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
            set_tests_properties(native_network_pressure_${sanitizer}_sanitize PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        endif()
        add_test(NAME native_sqlite_${sanitizer}_sanitize COMMAND "${CMAKE_COMMAND}"
            -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
            -DINPUT=${SOURCE_DIR}/tests/native_sqlite.wd -DSANITIZER=${sanitizer}
            -DSANITIZER_TIMEOUT=900 -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sqlite-${sanitizer}-sanitize
            -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
        set_tests_properties(native_sqlite_${sanitizer}_sanitize PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native_sanitize_obj_import COMMAND "${CMAKE_COMMAND}"
        -DCLANG=${CLANG} -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DINPUT=${SOURCE_DIR}/tests/obj_import.wd -DSOURCE_DIR=${SOURCE_DIR}
        -DRUN_ARGS=${SOURCE_DIR}/tests/fixtures/obj/tetrahedron.obj
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-obj-import
        -P "${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
    set_tests_properties(native_sanitize_obj_import PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_test(NAME native_sanitize_geometry COMMAND "${CMAKE_COMMAND}" -DCLANG=${CLANG}
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DSOURCE_DIR=${SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sanitize-geometry
        -P "${CMAKE_CURRENT_LIST_DIR}/array-sanitize.cmake")
    set_tests_properties(native_sanitize_geometry PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
endif()
foreach(level IN ITEMS 0 2)
    add_test(NAME native_math_O${level} COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLLC=${LLC} -DOPT=${OPT}
        -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT} -DLEVEL=${level}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/math-O${level}
        -P "${CMAKE_CURRENT_LIST_DIR}/math.cmake")
    set_tests_properties(native_math_O${level} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    add_test(NAME native_arrays_O${level} COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLLC=${LLC} -DOPT=${OPT}
        -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT} -DLEVEL=${level}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/arrays-O${level}
        -P "${CMAKE_CURRENT_LIST_DIR}/arrays.cmake")
    set_tests_properties(native_arrays_O${level} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
endforeach()
if(ENABLE_NATIVE_BOOTSTRAP)
    add_test(NAME native_bootstrap COMMAND "${CMAKE_COMMAND}" -DSEED=${EMITTER}
        -DSTDLIB=${STDLIB} -DRUNTIME_OBJECT=${RUNTIME_OBJECT} -DLLC=${LLC}
        -DOPT=${OPT} -DLINKER=${LINKER} -DSOURCE_DIR=${SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap
        -P "${CMAKE_CURRENT_LIST_DIR}/bootstrap.cmake")
    set_tests_properties(native_bootstrap PROPERTIES TIMEOUT 7200 RUN_SERIAL TRUE)
endif()
foreach(level IN ITEMS 0 2)
    add_test(NAME native_O${level} COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB}
        -DLLC=${LLC} -DOPT=${OPT} -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT}
        -DSOURCE_DIR=${SOURCE_DIR} -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/O${level}
        -DLEVEL=${level} -P "${CMAKE_CURRENT_LIST_DIR}/validation.cmake")
    set_tests_properties(native_O${level} PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
endforeach()
foreach(level IN ITEMS 0 2)
    add_test(NAME native_json_O${level} COMMAND "${CMAKE_COMMAND}"
        -DEMITTER=${EMITTER} -DSTDLIB=${STDLIB} -DLLC=${LLC} -DOPT=${OPT}
        -DLINKER=${LINKER} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
        -DRUNTIME_OBJECT=${RUNTIME_OBJECT}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/json-O${level} -DLEVEL=${level}
        -P "${CMAKE_CURRENT_LIST_DIR}/json.cmake")
    set_tests_properties(native_json_O${level} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
endforeach()
