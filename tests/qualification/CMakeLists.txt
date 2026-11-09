cmake_minimum_required(VERSION 3.25)
project(wood_selfhost_qualification LANGUAGES NONE)
enable_testing()

# Run preserved production suites against an installed candidate. This project
# neither builds nor replaces the production compiler. Keep all artifacts.
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(required IN ITEMS WOOD PRODUCTION FIXTURE_DIR BUNDLE_DIR HOST_CXX)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Supply -D${required}=<absolute path>")
    endif()
endforeach()
foreach(executable IN ITEMS WOOD PRODUCTION HOST_CXX)
    if(NOT EXISTS "${${executable}}")
        message(FATAL_ERROR "Missing ${executable}: ${${executable}}")
    endif()
endforeach()
file(SHA256 "${WOOD}" candidate_hash)
file(SHA256 "${PRODUCTION}" production_hash)
if(candidate_hash STREQUAL production_hash)
    message(FATAL_ERROR "Candidate and production reference must be distinct binaries")
endif()
set(suffix "")
if(WIN32)
    set(suffix ".exe")
endif()
foreach(tool IN ITEMS ffi_fixture obj_check backend_tests network_app_tests
        bootstrap_compiler_oracle bootstrap_semantics_oracle bootstrap_lexer_oracle
        bootstrap_parser_oracle selfhost_host_tests)
    if(tool STREQUAL "ffi_fixture")
        set(file "${FIXTURE_DIR}/wood_ffi_fixture.a")
        if(WIN32)
            set(file "${FIXTURE_DIR}/wood_ffi_fixture.lib")
        endif()
        # The normal GNU target has the conventional lib prefix.
        if(NOT EXISTS "${file}" AND NOT WIN32)
            set(file "${FIXTURE_DIR}/libwood_ffi_fixture.a")
        endif()
    else()
        set(file "${FIXTURE_DIR}/wood_${tool}${suffix}")
    endif()
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing qualification fixture: ${file}")
    endif()
    set(${tool} "${file}")
endforeach()
if(NOT DEFINED BOOTSTRAP_SANITIZER_OPTIMIZATION)
    set(BOOTSTRAP_SANITIZER_OPTIMIZATION 0)
endif()
set(common
    -DWOOD=${WOOD} -DPRODUCTION=${PRODUCTION} -DSOURCE_DIR=${SOURCE_DIR}
    -DSANITIZE_GENERATED=${SANITIZE_GENERATED}
    -DFFI_LIBRARY=${ffi_fixture} -DOBJ_CHECK=${obj_check}
    -DBACKEND_TEST=${backend_tests} -DNETWORK_TEST=${network_app_tests}
    -DHOST_CXX=${HOST_CXX} -DHOST_MSVC=${HOST_MSVC}
    -DSTDLIB_BUNDLE=${BUNDLE_DIR}/bootstrap-stdlib.json
    -DRUNTIME=${BUNDLE_DIR}/bootstrap-runtime.hpp
    -DORACLE=${bootstrap_compiler_oracle} -DKERNEL_ORACLE=${bootstrap_semantics_oracle}
    -DLEXER_ORACLE=${bootstrap_lexer_oracle} -DPARSER_ORACLE=${bootstrap_parser_oracle}
    -DBOOTSTRAP_SANITIZER_OPTIMIZATION=${BOOTSTRAP_SANITIZER_OPTIMIZATION}
    -DBINARY_DIR=${BUNDLE_DIR} -DCONFIG=Release -DPWSH=${PWSH})
set(qualification_environment "WOOD_CXX=${HOST_CXX}")
if(UNIX)
    list(APPEND qualification_environment "WOOD_REQUIRE_TLS_TESTS=1")
endif()
foreach(suite IN ITEMS selfhost_diagnostics selfhost_cli project_init projects explain operational integration
        applications obj_import native_foundations statistics time expressions
        typed_interfaces generic_records closures_iterators compiler_intelligence
        native_computing data_applications native_ecosystem backend networking
        selfhost_distribution selfhost_example
        bootstrap_project_trust bootstrap_compiler)
    set(script "${SOURCE_DIR}/tests/${suite}.cmake")
    if(suite MATCHES "^bootstrap_(compiler|project_trust)$")
        set(script "${SOURCE_DIR}/tests/selfhost_bootstrap_gate.cmake")
    endif()
    add_test(NAME ${suite} COMMAND "${CMAKE_COMMAND}" ${common}
        -DSUITE=${suite} -DQUALIFICATION_NATIVE_LOCK=${BUNDLE_DIR}/qualification-bootstrap.lock
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${suite}
        -P ${script})
    set_tests_properties(${suite} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE
        ENVIRONMENT "${qualification_environment}")
endforeach()
set_tests_properties(bootstrap_compiler PROPERTIES TIMEOUT 5400)
set_tests_properties(bootstrap_project_trust PROPERTIES TIMEOUT 3600)
set_tests_properties(selfhost_distribution PROPERTIES TIMEOUT 2400)
add_test(NAME selfhost_host COMMAND "${selfhost_host_tests}")
set_tests_properties(selfhost_host PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
