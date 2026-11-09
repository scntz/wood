cmake_minimum_required(VERSION 3.25)
project(wood VERSION 0.4.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <charconv>
int main() { char b[128]; return std::to_chars(b, b + 128, 1.5).ec == std::errc{} ? 0 : 1; }"
    WOOD_HAS_FLOAT_TO_CHARS)
if(NOT WOOD_HAS_FLOAT_TO_CHARS)
    message(FATAL_ERROR "Wood requires a standard library with floating-point std::to_chars support")
endif()
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
check_cxx_source_compiles("#include <stop_token>
#include <thread>
int main() {
    std::jthread worker([](std::stop_token token) { (void)token.stop_requested(); });
    worker.request_stop();
}" WOOD_HAS_NATIVE_THREADS)
cmake_pop_check_state()
if(NOT WOOD_HAS_NATIVE_THREADS)
    message(FATAL_ERROR "Wood's generated runtime requires std::jthread/std::stop_token and native thread support; select a compatible C++23 compiler and standard library")
endif()

include(CTest)
include(GNUInstallDirs)
option(WOOD_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(WOOD_SELFHOST_TESTS "Validate the separately built self-hosted driver against production CLI suites" OFF)
option(WOOD_RELEASE_BUILD "Remove checkout prefixes from generated compiler-origin diagnostics" OFF)
set(WOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION "1" CACHE STRING "Generated bootstrap sanitizer optimization: 0 or 1")
set_property(CACHE WOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION PROPERTY STRINGS 0 1)
if(NOT WOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION MATCHES "^[01]$")
    message(FATAL_ERROR "WOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION must be 0 or 1")
endif()
option(WOOD_BUILD_BENCHMARKS "Build the checked-runtime C++ benchmark reference" OFF)
option(WOOD_PYTHON_TESTS "Discover optional Python differential test oracles" ON)
if(MSVC AND WOOD_SANITIZERS)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_library(wood_support src/io.cpp src/hash.cpp)
target_include_directories(wood_support PUBLIC include)
target_compile_features(wood_support PUBLIC cxx_std_23)
add_library(wood_compiler
    src/diagnostic.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
    src/codegen.cpp src/modules.cpp src/project.cpp)
target_link_libraries(wood_compiler PUBLIC wood_support Threads::Threads)
target_include_directories(wood_compiler PUBLIC include)
target_compile_features(wood_compiler PUBLIC cxx_std_23)
set_target_properties(wood_compiler PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    # Bounded recursive frontend passes support 256 expression levels. Use an
    # explicit compiler stack budget, including sanitizer frame redzones, rather
    # than depending on the 1 MiB Windows executable default. This propagates
    # to frontend API consumers; generated Wood programs retain host defaults.
    target_link_options(wood_compiler INTERFACE /STACK:8388608)
endif()

# Embed the runtime so installed compilers do not depend on the source checkout.
include(tools/embed-text.cmake)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime/wood_runtime.hpp" WOOD_RUNTIME_SOURCE)
foreach(part IN ITEMS arrays callables native streams sqlite tls tasks random resources)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime/wood_${part}.hpp" WOOD_PART_SOURCE)
    string(REPLACE "#include \"wood_${part}.hpp\"" "${WOOD_PART_SOURCE}" WOOD_RUNTIME_SOURCE "${WOOD_RUNTIME_SOURCE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/wood_${part}.hpp")
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/wood/json.hpp" WOOD_JSON_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/wood/filesystem.hpp" WOOD_FILESYSTEM_SOURCE)
string(PREPEND WOOD_RUNTIME_SOURCE "${WOOD_FILESYSTEM_SOURCE}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/wood/filesystem.hpp")
string(PREPEND WOOD_RUNTIME_SOURCE "${WOOD_JSON_SOURCE}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/wood_runtime.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/wood_streams.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/wood/json.hpp")
wood_embed_text(WOOD_RUNTIME_LITERAL "${WOOD_RUNTIME_SOURCE}")
configure_file(src/runtime_source.hpp.in generated/wood/runtime_source.hpp @ONLY)
# Plain amalgamation for the Wood-written transitional emitter. This is the
# same runtime embedded by production; no frontend implementation is bundled.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.hpp" "${WOOD_RUNTIME_SOURCE}")
configure_file(src/config.hpp.in generated/wood/config.hpp @ONLY)
file(GLOB WOOD_STDLIB_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/*.wd")
set(WOOD_STDLIB_ROWS "")
set(WOOD_STDLIB_BUNDLE "{}")
foreach(source IN LISTS WOOD_STDLIB_FILES)
    get_filename_component(name "${source}" NAME_WE)
    file(READ "${source}" content)
    set(json_source "${content}")
    string(REPLACE "\\" "\\\\" json_source "${json_source}")
    string(REPLACE "\"" "\\\"" json_source "${json_source}")
    string(REPLACE "\n" "\\n" json_source "${json_source}")
    string(REPLACE "\r" "\\r" json_source "${json_source}")
    string(REPLACE "\t" "\\t" json_source "${json_source}")
    string(JSON WOOD_STDLIB_BUNDLE SET "${WOOD_STDLIB_BUNDLE}" "std.${name}" "\"${json_source}\"")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    wood_embed_text(literal "${content}")
    string(APPEND WOOD_STDLIB_ROWS "{\"std.${name}\", ${literal}},\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json" CONTENT "${WOOD_STDLIB_BUNDLE}\n")
configure_file(src/standard_sources.hpp.in generated/wood/standard_sources.hpp @ONLY)
target_include_directories(wood_compiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/language.json" WOOD_LANGUAGE_INFO)
wood_embed_text(WOOD_LANGUAGE_LITERAL "${WOOD_LANGUAGE_INFO}")
wood_embed_text(WOOD_STDLIB_LITERAL "${WOOD_STDLIB_BUNDLE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/docs/language.json")
configure_file(src/language_info.hpp.in generated/wood/language_info.hpp @ONLY)

add_executable(wood src/main.cpp src/process.cpp)
target_link_libraries(wood PRIVATE wood_compiler)
target_include_directories(wood PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

function(wood_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(WOOD_SANITIZERS)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(WOOD_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
wood_warnings(wood_compiler)
wood_warnings(wood_support)
wood_warnings(wood)
# Optional hosted Wood-written compiler. This seed target links only the native
# runtime emitted into its source; it does not link the C++ frontend library.
file(GLOB WOOD_BOOTSTRAP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/src/*.wd")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-compiler-seed.cpp"
    COMMAND $<TARGET_FILE:wood> emit-cpp "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/src/compiler_main.wd"
        -o "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-compiler-seed.cpp"
    DEPENDS wood ${WOOD_BOOTSTRAP_SOURCES} ${WOOD_STDLIB_FILES}
    VERBATIM)
add_executable(wood_bootstrap_compiler EXCLUDE_FROM_ALL "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-compiler-seed.cpp")
target_compile_features(wood_bootstrap_compiler PRIVATE cxx_std_23)
target_link_libraries(wood_bootstrap_compiler PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(wood_bootstrap_compiler PRIVATE /bigobj)
    target_link_options(wood_bootstrap_compiler PRIVATE /STACK:8388608 /OPT:NOICF)
endif()
wood_warnings(wood_bootstrap_compiler)

# Usable self-hosted driver candidate, isolated from the production executable.
# Its native bridge reuses OS implementations, never the C++ frontend library.
configure_file(src/driver_assets.hpp.in generated/wood/driver_assets.hpp @ONLY)
add_library(wood_driver_host STATIC EXCLUDE_FROM_ALL src/selfhost_host.cpp src/io.cpp src/process.cpp)
target_include_directories(wood_driver_host PRIVATE include src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(wood_driver_host PRIVATE cxx_std_23)
target_link_libraries(wood_driver_host PRIVATE Threads::Threads)
wood_warnings(wood_driver_host)
set(WOOD_SELFHOST_EMITTER "" CACHE FILEPATH "Preserved Wood-written four-argument emitter for independent driver bootstrapping")
if(WOOD_SELFHOST_EMITTER)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp"
    COMMAND ${CMAKE_COMMAND}
        -DEMITTER=${WOOD_SELFHOST_EMITTER}
        -DSTDLIB=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DRUNTIME=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.hpp
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/src/driver_main.wd
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/selfhost_seed.cmake
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DENABLED=${WOOD_RELEASE_BUILD}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/release-source-paths.cmake
    DEPENDS "${WOOD_SELFHOST_EMITTER}" ${WOOD_BOOTSTRAP_SOURCES} ${WOOD_STDLIB_FILES}
        "${CMAKE_CURRENT_BINARY_DIR}/generated/wood/runtime_source.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/selfhost_seed.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/release-source-paths.cmake"
    VERBATIM)
else()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp"
    COMMAND $<TARGET_FILE:wood> emit-cpp "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/src/driver_main.wd"
        -o "${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DENABLED=${WOOD_RELEASE_BUILD}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/release-source-paths.cmake
    DEPENDS wood ${WOOD_BOOTSTRAP_SOURCES} ${WOOD_STDLIB_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/release-source-paths.cmake"
    VERBATIM)
endif()
add_executable(wood_selfhost EXCLUDE_FROM_ALL "${CMAKE_CURRENT_BINARY_DIR}/selfhost-driver-seed.cpp")
target_compile_features(wood_selfhost PRIVATE cxx_std_23)
target_link_libraries(wood_selfhost PRIVATE wood_driver_host Threads::Threads ${CMAKE_DL_LIBS})
set(WOOD_SELFHOST_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/selfhost-candidate" CACHE PATH "Isolated self-hosted candidate executable output")
set_target_properties(wood_selfhost PROPERTIES OUTPUT_NAME wood-selfhost
    RUNTIME_OUTPUT_DIRECTORY "${WOOD_SELFHOST_OUTPUT_DIRECTORY}")
if(MSVC)
    target_compile_options(wood_selfhost PRIVATE /bigobj)
    target_link_options(wood_selfhost PRIVATE /STACK:8388608 /OPT:NOICF)
    # Match the existing CLI's generated-program CRT for self-rebuilding via FFI.
    set_property(TARGET wood_driver_host wood_selfhost PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
wood_warnings(wood_selfhost)
# Keep offline documentation links useful and ship the small, inspectable public
# implementation alongside either compiler. Never install build trees/caches.
function(wood_install_public_sources destination)
    install(FILES README.md LICENSE LICENSE-MIT LICENSE-APACHE THIRD_PARTY_NOTICES.md
        AGENTS.md CONTRIBUTING.md CODE_OF_CONDUCT.md SECURITY.md
        CHANGELOG.md CMakeLists.txt .gitignore .gitattributes
        .editorconfig .clang-format DESTINATION ${destination} ${ARGN})
    install(DIRECTORY include src runtime tests tools bootstrap benchmarks .github packages
        DESTINATION ${destination} ${ARGN} FILES_MATCHING
        PATTERN "*.cpp" PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.c"
        PATTERN "*.inc" PATTERN "*.ll" PATTERN "*.wd" PATTERN "*.cmake"
        PATTERN "*.in" PATTERN "*.json" PATTERN "*.md" PATTERN "*.py"
        PATTERN "*.ps1" PATTERN "*.sh" PATTERN "*.yml" PATTERN "*.yaml"
        PATTERN "*.ebnf" PATTERN "*.jsonl" PATTERN "CMakeLists.txt" PATTERN wood.lock PATTERN COPYRIGHT PATTERN "LICENSE*"
        PATTERN "CMakeFiles" EXCLUDE PATTERN "Testing" EXCLUDE
        PATTERN "results" EXCLUDE
        PATTERN "build" EXCLUDE PATTERN "out" EXCLUDE PATTERN "*.stages" EXCLUDE
        PATTERN "__pycache__" EXCLUDE PATTERN "*.local*" EXCLUDE)
    install(DIRECTORY tests/fixtures DESTINATION ${destination}/tests ${ARGN})
endfunction()
install(TARGETS wood_selfhost RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT SelfHosted EXCLUDE_FROM_ALL)
install(TARGETS wood_driver_host ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT SelfHosted EXCLUDE_FROM_ALL)
install(DIRECTORY docs stdlib examples DESTINATION ${CMAKE_INSTALL_DATADIR}/wood
    COMPONENT SelfHosted EXCLUDE_FROM_ALL
    PATTERN "*.zip" EXCLUDE PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
wood_install_public_sources(${CMAKE_INSTALL_DATADIR}/wood COMPONENT SelfHosted EXCLUDE_FROM_ALL)
if(WOOD_BUILD_BENCHMARKS)
    add_executable(wood_bench_reference benchmarks/reference.cpp)
    add_executable(wood_bench_dictionary benchmarks/reference_dictionary.cpp)
    add_executable(wood_bench_string_length benchmarks/reference_string_length.cpp)
    add_executable(wood_bench_callbacks benchmarks/reference_callbacks.cpp)
    add_executable(wood_bench_backend benchmarks/reference_backend.cpp)
    foreach(target IN ITEMS wood_bench_reference wood_bench_dictionary wood_bench_string_length wood_bench_callbacks wood_bench_backend)
        target_include_directories(${target} PRIVATE runtime)
        target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        if(MSVC)
            # Match the CLI's default static CRT selection for generated programs.
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        wood_warnings(${target})
    endforeach()
endif()

install(TARGETS wood RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
wood_install_public_sources(${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs examples stdlib DESTINATION ${CMAKE_INSTALL_DOCDIR}
    PATTERN "*.zip" EXCLUDE PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)

# The core release archive is relocatable and has no Python component. The
# optional Python-enabled profile remains a separately specified future artifact.
set(CPACK_PACKAGE_NAME "wood")
set(CPACK_PACKAGE_VENDOR "Wood contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Wood native compiler, libraries and development documentation")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dist")
set(CPACK_PACKAGE_FILE_NAME "wood-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)

if(BUILD_TESTING)
    add_test(NAME official_packages COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/official-packages -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/official_packages.cmake)
    set_tests_properties(official_packages PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    add_test(NAME community_packages COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/community-packages -P
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/community_packages.cmake)
    set_tests_properties(community_packages PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
    # Package commands belong to the Wood-written driver, so this runs against a
    # supplied native installation: -DWOOD_NATIVE_FOR_PACKAGE_TESTS=path/to/wood.
    if(WOOD_NATIVE_FOR_PACKAGE_TESTS)
        add_test(NAME package_manager COMMAND ${CMAKE_COMMAND}
            -DWOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/package-manager -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manager.cmake)
        set_tests_properties(package_manager PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        add_test(NAME official_fetch COMMAND ${CMAKE_COMMAND}
            -DWOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/official-fetch -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/official_fetch.cmake)
        set_tests_properties(official_fetch PROPERTIES TIMEOUT 900)
        add_test(NAME diagnostics_quality COMMAND ${CMAKE_COMMAND}
            -DWOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/diagnostics-quality -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics_quality.cmake)
        add_test(NAME runtime_heap COMMAND ${WOOD_NATIVE_FOR_PACKAGE_TESTS} run ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_heap.wd)
        set_tests_properties(runtime_heap PROPERTIES PASS_REGULAR_EXPRESSION "heap ok" TIMEOUT 300)
        add_test(NAME math_functions COMMAND ${WOOD_NATIVE_FOR_PACKAGE_TESTS} run ${CMAKE_CURRENT_SOURCE_DIR}/tests/math_functions.wd)
        set_tests_properties(math_functions PROPERTIES PASS_REGULAR_EXPRESSION "math functions ok" TIMEOUT 300)
        add_test(NAME error_explanations COMMAND ${CMAKE_COMMAND} -DWOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/error-explanations -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/explanations.cmake)
        set_tests_properties(error_explanations PROPERTIES TIMEOUT 900)
        add_test(NAME formatter COMMAND ${CMAKE_COMMAND} -DWOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/formatter
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter.cmake)
        set_tests_properties(formatter PROPERTIES TIMEOUT 600)
        find_program(WOOD_NODE NAMES node)
        if(WOOD_NODE)
            add_test(NAME lsp_protocol COMMAND ${CMAKE_COMMAND} -E env WOOD=${WOOD_NATIVE_FOR_PACKAGE_TESTS}
                ${WOOD_NODE} --test --test-timeout=240000 ${CMAKE_CURRENT_SOURCE_DIR}/tests/lsp/protocol.test.mjs)
            set_tests_properties(lsp_protocol PROPERTIES TIMEOUT 600)
            # Needs the output of tools/playground/build.sh, which needs clang's WebAssembly target.
            set(WOOD_PLAYGROUND_DIST "" CACHE PATH "Directory built by tools/playground/build.sh")
            if(WOOD_PLAYGROUND_DIST)
                add_test(NAME playground_examples COMMAND ${WOOD_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/playground/examples.mjs
                    ${WOOD_NATIVE_FOR_PACKAGE_TESTS} ${WOOD_PLAYGROUND_DIST})
                set_tests_properties(playground_examples PROPERTIES TIMEOUT 600)
            endif()
        endif()
    endif()
    add_test(NAME desktop_presentation COMMAND $<TARGET_FILE:wood> test
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/desktop_notes" --locked --report=json)
    set_tests_properties(desktop_presentation PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME stdlib_contracts COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib.cmake)
    set_tests_properties(stdlib_contracts PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
    add_test(NAME embedded_text COMMAND ${CMAKE_COMMAND}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/embedded_text.cmake")
    # Exercise every generated embedded asset, including the optional driver's
    # bundle. The synthetic value crosses a token boundary inside UTF-8 and
    # includes a delimiter collision, quotes, semicolons and more than 64 KiB.
    string(REPEAT "x" 7999 embed_prefix)
    string(REPEAT "body;\"\\\n" 9000 embed_tail)
    set(embed_fixture "${embed_prefix}🌳é)WOOD_0\"${embed_tail}")
    wood_embed_text(embed_fixture_literal "${embed_fixture}")
    wood_embed_text(embed_empty_literal "")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embedded-fixture.txt" "${embed_fixture}")
    # bootstrap-stdlib.json deliberately adds a file-framing newline, whereas
    # the embedded driver bundle is exactly the JSON value without that suffix.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embedded-stdlib.json" "${WOOD_STDLIB_BUNDLE}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/wood/embedded_fixture.hpp"
        "#pragma once\n#include <string_view>\nnamespace wood {\ninline constexpr char embedded_fixture_data[] = ${embed_fixture_literal};\ninline constexpr std::string_view embedded_fixture{embedded_fixture_data, sizeof(embedded_fixture_data)-1};\ninline constexpr std::string_view embedded_empty = ${embed_empty_literal};\n}\n")
    add_executable(wood_embedded_asset_tests tests/embedded_assets.cpp)
    target_include_directories(wood_embedded_asset_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    wood_warnings(wood_embedded_asset_tests)
    add_test(NAME embedded_assets COMMAND wood_embedded_asset_tests
        "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/language.json"
        "${CMAKE_CURRENT_BINARY_DIR}/embedded-stdlib.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        "${CMAKE_CURRENT_BINARY_DIR}/embedded-fixture.txt")
    if(WOOD_SELFHOST_TESTS)
        add_executable(wood_selfhost_host_tests tests/selfhost_host_tests.cpp)
        target_include_directories(wood_selfhost_host_tests PRIVATE include)
        target_link_libraries(wood_selfhost_host_tests PRIVATE wood_driver_host Threads::Threads)
        target_compile_features(wood_selfhost_host_tests PRIVATE cxx_std_23)
        if(MSVC)
            set_property(TARGET wood_selfhost_host_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        wood_warnings(wood_selfhost_host_tests)
        add_test(NAME selfhost_host COMMAND wood_selfhost_host_tests)
        add_test(NAME selfhost_cli COMMAND ${CMAKE_COMMAND}
            -DWOOD=$<TARGET_FILE:wood_selfhost>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/selfhost-cli
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/selfhost_cli.cmake)
        set_tests_properties(selfhost_cli PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
        add_test(NAME selfhost_distribution COMMAND ${CMAKE_COMMAND}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DCONFIG=$<CONFIG>
            -DPRODUCTION=$<TARGET_FILE:wood>
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/selfhost-distribution
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/selfhost_distribution.cmake)
        set_tests_properties(selfhost_distribution PROPERTIES TIMEOUT 2400 RUN_SERIAL TRUE)
        foreach(suite IN ITEMS project_init projects explain operational integration applications obj_import native_foundations)
            add_test(NAME selfhost_${suite} COMMAND ${CMAKE_COMMAND}
                -DWOOD=$<TARGET_FILE:wood_selfhost>
                -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
                -DFFI_LIBRARY=$<TARGET_FILE:wood_ffi_fixture>
                -DOBJ_CHECK=$<TARGET_FILE:wood_obj_check>
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/selfhost-${suite}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/${suite}.cmake)
            set_tests_properties(selfhost_${suite} PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        endforeach()
    endif()
    add_test(NAME bootstrap_project_trust COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSTDLIB_BUNDLE=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DRUNTIME=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.hpp
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-project-trust
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_project_trust.cmake)
    set_tests_properties(bootstrap_project_trust PROPERTIES TIMEOUT 3600)
    add_executable(wood_bootstrap_compiler_oracle tests/bootstrap_compiler_oracle.cpp)
    target_link_libraries(wood_bootstrap_compiler_oracle PRIVATE wood_compiler)
    wood_warnings(wood_bootstrap_compiler_oracle)
    add_test(NAME bootstrap_compiler COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_compiler_oracle>
        -DKERNEL_ORACLE=$<TARGET_FILE:wood_bootstrap_semantics_oracle>
        -DLEXER_ORACLE=$<TARGET_FILE:wood_bootstrap_lexer_oracle>
        -DPARSER_ORACLE=$<TARGET_FILE:wood_bootstrap_parser_oracle>
        -DFFI_LIBRARY=$<TARGET_FILE:wood_ffi_fixture>
        -DOBJ_CHECK=$<TARGET_FILE:wood_obj_check>
        -DBACKEND_TEST=$<TARGET_FILE:wood_backend_tests>
        -DBOOTSTRAP_SANITIZER_OPTIMIZATION=${WOOD_BOOTSTRAP_SANITIZER_OPTIMIZATION}
        -DSTDLIB_BUNDLE=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DRUNTIME=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.hpp
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-compiler
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_compiler.cmake)
    set_tests_properties(bootstrap_compiler PROPERTIES TIMEOUT 5400)
    add_executable(wood_bootstrap_semantics_oracle tests/bootstrap_semantics_oracle.cpp)
    target_link_libraries(wood_bootstrap_semantics_oracle PRIVATE wood_compiler)
    wood_warnings(wood_bootstrap_semantics_oracle)
    add_test(NAME bootstrap_semantics COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_semantics_oracle>
        -DSTDLIB_BUNDLE=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-semantics
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_semantics.cmake)
    set_tests_properties(bootstrap_semantics PROPERTIES TIMEOUT 1800)
    add_executable(wood_bootstrap_signatures_oracle tests/bootstrap_signatures_oracle.cpp)
    target_link_libraries(wood_bootstrap_signatures_oracle PRIVATE wood_compiler)
    wood_warnings(wood_bootstrap_signatures_oracle)
    add_test(NAME bootstrap_signatures COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_signatures_oracle>
        -DMODULE_ORACLE=$<TARGET_FILE:wood_bootstrap_modules_oracle>
        -DSTDLIB_BUNDLE=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-signatures
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_signatures.cmake)
    set_tests_properties(bootstrap_signatures PROPERTIES TIMEOUT 1800)
    add_executable(wood_bootstrap_modules_oracle tests/bootstrap_modules_oracle.cpp)
    target_link_libraries(wood_bootstrap_modules_oracle PRIVATE wood_compiler)
    target_include_directories(wood_bootstrap_modules_oracle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    wood_warnings(wood_bootstrap_modules_oracle)
    add_test(NAME bootstrap_modules COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_modules_oracle>
        -DSTDLIB_BUNDLE=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-stdlib.json
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-modules
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_modules.cmake)
    set_tests_properties(bootstrap_modules PROPERTIES TIMEOUT 1800)
    add_executable(wood_bootstrap_parser_oracle tests/bootstrap_parser_oracle.cpp)
    target_link_libraries(wood_bootstrap_parser_oracle PRIVATE wood_compiler)
    wood_warnings(wood_bootstrap_parser_oracle)
    add_test(NAME bootstrap_parser COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_parser_oracle>
        -DLEXER_ORACLE=$<TARGET_FILE:wood_bootstrap_lexer_oracle>
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DHOST_MSVC=${MSVC}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-parser
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_parser.cmake)
    set_tests_properties(bootstrap_parser PROPERTIES TIMEOUT 900)
    if(MSVC AND WOOD_SANITIZERS)
        # The fully instrumented Debug reference and process-per-input corpus
        # took 878 seconds locally; retain headroom for slower CI/developer PCs.
        set_tests_properties(bootstrap_parser PROPERTIES TIMEOUT 1800)
    endif()
    add_executable(wood_bootstrap_lexer_oracle tests/bootstrap_lexer_oracle.cpp)
    target_link_libraries(wood_bootstrap_lexer_oracle PRIVATE wood_compiler)
    wood_warnings(wood_bootstrap_lexer_oracle)
    add_test(NAME bootstrap_lexer COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DORACLE=$<TARGET_FILE:wood_bootstrap_lexer_oracle>
        -DHOST_CXX=${CMAKE_CXX_COMPILER}
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-lexer
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_lexer.cmake)
    set_tests_properties(bootstrap_lexer PROPERTIES TIMEOUT 600)
    find_program(WOOD_PWSH_EXECUTABLE NAMES pwsh DOC "Optional PowerShell 7 for independent .NET TLS tests")
    add_library(wood_ffi_fixture STATIC tests/ffi_fixture.cpp)
    # This models an independently built native dependency. Do not inject Wood's
    # ASan/UBSan flags into its archive: normal generated links are unsanitized.
    if(MSVC)
        target_compile_options(wood_ffi_fixture PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wood_ffi_fixture PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME native_ecosystem COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DFFI_LIBRARY=$<TARGET_FILE:wood_ffi_fixture>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-ecosystem
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_ecosystem.cmake)
    set_tests_properties(native_ecosystem PROPERTIES TIMEOUT 360)
    add_executable(wood_process_tests tests/process_tests.cpp src/process.cpp)
    target_link_libraries(wood_process_tests PRIVATE wood_support)
    wood_warnings(wood_process_tests)
    add_test(NAME process COMMAND wood_process_tests)
    set_tests_properties(process PROPERTIES TIMEOUT 30)
    add_executable(wood_support_tests tests/support_tests.cpp)
    target_link_libraries(wood_support_tests PRIVATE wood_support)
    wood_warnings(wood_support_tests)
    add_test(NAME support COMMAND wood_support_tests)
    add_executable(wood_project_tests tests/project_tests.cpp)
    target_link_libraries(wood_project_tests PRIVATE wood_compiler)
    wood_warnings(wood_project_tests)
    add_test(NAME project_unit COMMAND wood_project_tests)
    add_test(NAME project_init COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/project-init
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/project_init.cmake)
    set_tests_properties(project_init PROPERTIES TIMEOUT 240)
    # Compile the exact C++ example in the C ABI manual, not a duplicate spelling.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/native-ffi.md" docs_ffi_manual)
    string(REGEX MATCH "```cpp\n([^`]+)```" docs_ffi_match "${docs_ffi_manual}")
    if(NOT docs_ffi_match)
        message(FATAL_ERROR "C ABI manual must retain its compilable C++ example")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/documentation-ffi.cpp" "${CMAKE_MATCH_1}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/native-ffi.md")
    add_library(wood_documentation_ffi STATIC
        "${CMAKE_CURRENT_BINARY_DIR}/documentation-ffi.cpp" tests/documentation_ffi.cpp)
    set_property(TARGET wood_documentation_ffi PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_test(NAME documentation COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DDOCS_FFI_LIBRARY=$<TARGET_FILE:wood_documentation_ffi>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/documentation
        -DDOCS_SANITIZE=${WOOD_SANITIZERS}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/documentation.cmake)
    set_tests_properties(documentation PROPERTIES TIMEOUT 3600 LABELS documentation)
    set_tests_properties(support project_unit PROPERTIES TIMEOUT 30)
    if(WOOD_PYTHON_TESTS)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    if(WOOD_PYTHON_TESTS AND Python3_Interpreter_FOUND)
        add_test(NAME differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/differential.py $<TARGET_FILE:wood_support_tests>)
        set_tests_properties(differential PROPERTIES TIMEOUT 120)
        add_test(NAME csv_differential COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/csv_differential.py $<TARGET_FILE:wood>
            ${CMAKE_CURRENT_BINARY_DIR}/csv-differential)
        set_tests_properties(csv_differential PROPERTIES TIMEOUT 240)
        add_test(NAME expression_differential COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/expression_differential.py $<TARGET_FILE:wood>
            ${CMAKE_CURRENT_BINARY_DIR}/expression-differential ${WOOD_SANITIZERS})
        set_tests_properties(expression_differential PROPERTIES TIMEOUT 300)
        add_test(NAME typed_differential COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/typed_differential.py $<TARGET_FILE:wood>
            ${CMAKE_CURRENT_BINARY_DIR}/typed-differential ${WOOD_SANITIZERS})
        set_tests_properties(typed_differential PROPERTIES TIMEOUT 300)
    endif()
    add_executable(wood_tests tests/compiler_tests.cpp)
    target_link_libraries(wood_tests PRIVATE wood_compiler)
    wood_warnings(wood_tests)
    add_test(NAME compiler COMMAND wood_tests)
    set_tests_properties(compiler PROPERTIES TIMEOUT 30)
    add_executable(wood_explain_tests tests/explain_tests.cpp)
    target_include_directories(wood_explain_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(wood_explain_tests PRIVATE wood_compiler)
    wood_warnings(wood_explain_tests)
    add_test(NAME explain_unit COMMAND wood_explain_tests)
    set_tests_properties(explain_unit PROPERTIES TIMEOUT 30)
    add_test(NAME explain COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/explain
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/explain.cmake)
    set_tests_properties(explain PROPERTIES TIMEOUT 180)
    if(MSVC AND WOOD_SANITIZERS)
        # Debug STL plus ASan exercises all instantiation/capture boundaries.
        # Keep the same corpus and language limits with room for instrumented work.
        set_tests_properties(compiler PROPERTIES TIMEOUT 120)
    endif()
    add_executable(wood_runtime_tests tests/runtime_tests.cpp)
    target_include_directories(wood_runtime_tests PRIVATE runtime)
    target_link_libraries(wood_runtime_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(wood_runtime_tests PRIVATE cxx_std_23)
    wood_warnings(wood_runtime_tests)
    add_test(NAME runtime COMMAND wood_runtime_tests)
    set_tests_properties(runtime PROPERTIES TIMEOUT 30)
    add_executable(wood_array_tests tests/arrays_tests.cpp)
    target_include_directories(wood_array_tests PRIVATE runtime)
    target_link_libraries(wood_array_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(wood_array_tests PRIVATE cxx_std_23)
    wood_warnings(wood_array_tests)
    add_test(NAME arrays_runtime COMMAND wood_array_tests)
    set_tests_properties(arrays_runtime PROPERTIES TIMEOUT 60)
    add_executable(wood_performance_runtime_tests tests/performance_runtime_tests.cpp)
    target_include_directories(wood_performance_runtime_tests PRIVATE runtime benchmarks)
    target_link_libraries(wood_performance_runtime_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(wood_performance_runtime_tests PRIVATE cxx_std_23)
    wood_warnings(wood_performance_runtime_tests)
    if(WIN32 AND NOT MSVC)
        target_link_libraries(wood_performance_runtime_tests PRIVATE psapi)
    endif()
    add_test(NAME performance_runtime COMMAND wood_performance_runtime_tests)
    set_tests_properties(performance_runtime PROPERTIES TIMEOUT 30)
    add_test(NAME performance COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/performance
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance.cmake)
    set_tests_properties(performance PROPERTIES TIMEOUT 240)
    add_executable(wood_native_memory_tests tests/native_memory_tests.cpp)
    target_include_directories(wood_native_memory_tests PRIVATE benchmarks)
    target_compile_features(wood_native_memory_tests PRIVATE cxx_std_23)
    wood_warnings(wood_native_memory_tests)
    if(WIN32 AND NOT MSVC)
        target_link_libraries(wood_native_memory_tests PRIVATE psapi)
    endif()
    add_test(NAME native_memory_instrumentation COMMAND wood_native_memory_tests)
    add_executable(wood_obj_check tests/obj_check.cpp)
    target_link_libraries(wood_obj_check PRIVATE wood_support)
    wood_warnings(wood_obj_check)
    add_executable(wood_stream_tests tests/stream_tests.cpp)
    target_include_directories(wood_stream_tests PRIVATE runtime)
    target_link_libraries(wood_stream_tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    wood_warnings(wood_stream_tests)
    add_test(NAME streams_runtime COMMAND wood_stream_tests)
    set_tests_properties(streams_runtime PROPERTIES TIMEOUT 30)
    add_executable(wood_backend_tests tests/backend_tests.cpp src/process.cpp)
    add_executable(wood_output_fixture tests/native/output_fixture.cpp)
    wood_warnings(wood_output_fixture)
    target_include_directories(wood_backend_tests PRIVATE runtime)
    target_link_libraries(wood_backend_tests PRIVATE wood_support Threads::Threads ${CMAKE_DL_LIBS})
    wood_warnings(wood_backend_tests)
    add_test(NAME backend_runtime COMMAND wood_backend_tests core "${CMAKE_CURRENT_BINARY_DIR}/backend-runtime")
    set_tests_properties(backend_runtime PROPERTIES TIMEOUT 30)
    add_test(NAME tls_runtime COMMAND wood_backend_tests tls "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tls")
    set_tests_properties(tls_runtime PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    add_test(NAME backend COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DBACKEND_TEST=$<TARGET_FILE:wood_backend_tests>
        -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
        -DPWSH=${WOOD_PWSH_EXECUTABLE}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/backend
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/backend.cmake)
    set_tests_properties(backend PROPERTIES TIMEOUT 480)
    if(WOOD_PWSH_EXECUTABLE)
        add_test(NAME release_audit_binary COMMAND ${CMAKE_COMMAND}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/release-audit-binary
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_audit_binary.cmake)
        set_tests_properties(release_audit_binary PROPERTIES TIMEOUT 300)
    endif()
    add_executable(wood_network_app_tests tests/network_app_tests.cpp src/process.cpp)
    target_include_directories(wood_network_app_tests PRIVATE runtime)
    target_link_libraries(wood_network_app_tests PRIVATE wood_support Threads::Threads ${CMAKE_DL_LIBS})
    wood_warnings(wood_network_app_tests)
    add_test(NAME networking COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DNETWORK_TEST=$<TARGET_FILE:wood_network_app_tests>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/networking
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/networking.cmake)
    set_tests_properties(networking PROPERTIES TIMEOUT 360)
    add_test(NAME integration COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/integration
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.cmake)
    set_tests_properties(integration PROPERTIES TIMEOUT 240)
    add_test(NAME applications COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/applications
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/applications.cmake)
    set_tests_properties(applications PROPERTIES TIMEOUT 240)
    add_test(NAME projects COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/projects
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/projects.cmake)
    set_tests_properties(projects PROPERTIES TIMEOUT 300)
    add_test(NAME data_applications COMMAND ${CMAKE_COMMAND}
        -DWOOD=$<TARGET_FILE:wood>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/data-applications
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/data_applications.cmake)
    set_tests_properties(data_applications PROPERTIES TIMEOUT 300)
    foreach(suite IN ITEMS native_foundations operational statistics time expressions typed_interfaces generic_records closures_iterators compiler_intelligence native_computing obj_import)
        add_test(NAME ${suite} COMMAND ${CMAKE_COMMAND}
            -DWOOD=$<TARGET_FILE:wood>
            -DSANITIZE_GENERATED=${WOOD_SANITIZERS}
            -DFFI_LIBRARY=$<TARGET_FILE:wood_ffi_fixture>
            -DOBJ_CHECK=$<TARGET_FILE:wood_obj_check>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${suite}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/${suite}.cmake)
        set_tests_properties(${suite} PROPERTIES TIMEOUT 360)
    endforeach()
    # The maximum-width native tuple exercises expensive host template work.
    # Isolate it from concurrent native compiler jobs within this CTest run.
    set_tests_properties(typed_interfaces PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endif()
