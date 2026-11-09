cmake_minimum_required(VERSION 3.25)
foreach(required IN ITEMS EMITTER ASSETS RUNTIME_OBJECT TARGET PREFIX LLVM_DIR LINKER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Supply ${required}")
    endif()
endforeach()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PREFIX "${PREFIX}" ABSOLUTE)
if(EXISTS "${PREFIX}" OR IS_SYMLINK "${PREFIX}")
    message(FATAL_ERROR "Native install prefix must not exist: ${PREFIX}")
endif()
if(NOT "${TARGET}" MATCHES "^(windows|linux)-x86_64$")
    message(FATAL_ERROR "Unsupported native installation target")
endif()
get_filename_component(parent "${PREFIX}" DIRECTORY)
if(NOT IS_DIRECTORY "${parent}")
    message(FATAL_ERROR "Installation parent must exist")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef id)
set(stage "${PREFIX}.stages/${id}")
set(data "${stage}/share/wood")
file(MAKE_DIRECTORY "${stage}/bin" "${data}/bootstrap" "${data}/tools")
foreach(name IN ITEMS stdlib.json language.json explanations.json)
    if(NOT EXISTS "${ASSETS}/${name}")
        message(FATAL_ERROR "Missing prepared asset: ${name}")
    endif()
    file(COPY "${ASSETS}/${name}" DESTINATION "${data}")
endforeach()
file(COPY "${root}/bootstrap/src" "${root}/bootstrap/tests" "${root}/bootstrap/wood.json" DESTINATION "${data}/bootstrap")
file(COPY "${root}/runtime/native" DESTINATION "${data}" FILES_MATCHING
    PATTERN "*.c" PATTERN "*.h" PATTERN "*.inc" PATTERN "*.ll" PATTERN "*.md" PATTERN "*.json" PATTERN COPYRIGHT)
file(COPY "${root}/tools/prepare-native-runtime.cmake" DESTINATION "${data}/tools")
file(COPY "${root}/docs" DESTINATION "${data}" FILES_MATCHING
    PATTERN "*.md" PATTERN "*.json" PATTERN "*.ebnf" PATTERN "*.wd"
    PATTERN "*.patch" PATTERN "*.txt" PATTERN "*.svg" PATTERN "*.png"
    PATTERN "*.jpg" PATTERN "*.jpeg" PATTERN "*.gif" PATTERN "*.webp"
    PATTERN "*.pdf" PATTERN "*.html" PATTERN "*.css" PATTERN "*.js"
    PATTERN "*.tex" PATTERN "*.bib"
    PATTERN build EXCLUDE PATTERN out EXCLUDE PATTERN "*.stages" EXCLUDE)
file(COPY "${root}/examples" DESTINATION "${data}" FILES_MATCHING
    PATTERN "*.wd" PATTERN "*.json" PATTERN "*.jsonl" PATTERN "*.csv"
    PATTERN "*.obj" PATTERN "*.md" PATTERN "*.txt" PATTERN wood.lock
    PATTERN build EXCLUDE PATTERN out EXCLUDE PATTERN "*.stages" EXCLUDE)
file(COPY "${root}/stdlib" DESTINATION "${data}")
file(COPY "${root}/tests/fixtures" DESTINATION "${data}/tests")
# Retain the public source layout for offline documentation and self-rebuilding.
# Match source extensions explicitly; never copy checkout/build caches, binaries
# or local benchmark results into an installation.
file(COPY "${root}/include" "${root}/src" "${root}/runtime" "${root}/tests"
    "${root}/tools" "${root}/bootstrap" "${root}/benchmarks" "${root}/.github" "${root}/packages"
    DESTINATION "${data}" FILES_MATCHING
    PATTERN "*.cpp" PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.c"
    PATTERN "*.inc" PATTERN "*.ll" PATTERN "*.wd" PATTERN "*.cmake"
    PATTERN "*.in" PATTERN "*.json" PATTERN "*.md" PATTERN "*.py"
    PATTERN "*.ps1" PATTERN "*.sh" PATTERN "*.yml" PATTERN "*.yaml"
    PATTERN "*.ebnf" PATTERN "*.jsonl" PATTERN CMakeLists.txt PATTERN wood.lock PATTERN COPYRIGHT PATTERN "LICENSE*"
    PATTERN CMakeFiles EXCLUDE PATTERN Testing EXCLUDE PATTERN results EXCLUDE
    PATTERN out EXCLUDE PATTERN build EXCLUDE PATTERN "*.stages" EXCLUDE
    PATTERN __pycache__ EXCLUDE PATTERN "*.local*" EXCLUDE)
foreach(name IN ITEMS README.md AGENTS.md CONTRIBUTING.md
    CODE_OF_CONDUCT.md SECURITY.md CHANGELOG.md CMakeLists.txt
    .gitignore .gitattributes .editorconfig .clang-format)
    file(COPY "${root}/${name}" DESTINATION "${data}")
endforeach()
file(COPY "${root}/LICENSE" "${root}/LICENSE-MIT" "${root}/LICENSE-APACHE" "${root}/THIRD_PARTY_NOTICES.md" DESTINATION "${data}")
file(COPY_FILE "${RUNTIME_OBJECT}" "${data}/native/managed.o")
if(DEFINED CPP_RUNTIME AND NOT CPP_RUNTIME STREQUAL "")
    file(COPY_FILE "${CPP_RUNTIME}" "${data}/runtime.hpp")
endif()
set(config "{}")
string(JSON config SET "${config}" format 1)
string(JSON config SET "${config}" runtime_abi 4)
foreach(name IN ITEMS TARGET LLVM_DIR LINKER WSL_DISTRIBUTION)
    set(value "${${name}}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(key "${name}")
    string(TOLOWER "${key}" key)
    if("${name}" STREQUAL "WSL_DISTRIBUTION")
        set(key llvm_wsl)
    endif()
    string(JSON config SET "${config}" "${key}" "\"${value}\"")
endforeach()
file(WRITE "${data}/native.json" "${config}\n")
set(binary "${stage}/bin/wood")
set(build_binary "${PREFIX}.stages/compiler-${id}")
if("${TARGET}" STREQUAL "windows-x86_64")
    string(APPEND binary ".exe")
    string(APPEND build_binary ".exe")
endif()
if(SEED_IS_DRIVER)
    set(command "${EMITTER}" build "${data}/bootstrap/src/driver_main.wd" --backend=llvm -O0 -o "${build_binary}" --native-home "${data}" --llvm-dir "${LLVM_DIR}" --linker "${LINKER}")
    if(WSL_DISTRIBUTION)
        list(APPEND command --llvm-wsl "${WSL_DISTRIBUTION}")
    endif()
else()
    set(command "${CMAKE_COMMAND}" -DEMITTER=${EMITTER} -DSTDLIB=${data}/stdlib.json
        -DRUNTIME_DIR=${data}/native -DRUNTIME_OBJECT=${data}/native/managed.o
        -DTARGET=${TARGET} -DINPUT=${data}/bootstrap/src/driver_main.wd -DOUTPUT=${build_binary}
        -DLLC=${LLVM_DIR}/llc -DOPT=${LLVM_DIR}/opt -DLINKER=${LINKER}
        -DWSL_DISTRIBUTION=${WSL_DISTRIBUTION} -DOPTIMIZATION=0 -DSTAGE_TIMEOUT=1800
        -P "${root}/tools/build-native.cmake")
endif()
file(WRITE "${PREFIX}.stages/command-${id}.txt" "${command}\n")
execute_process(COMMAND ${command} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 3600 ENCODING UTF-8)
file(WRITE "${PREFIX}.stages/build-${id}.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Native driver installation build failed (${status}); prefix unchanged, evidence: ${stage}: ${out}${err}")
endif()
file(COPY_FILE "${build_binary}" "${binary}")
execute_process(COMMAND "${binary}" --language-info RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60 ENCODING UTF-8)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Installed driver metadata execution failed: ${status}: ${out}${err}")
endif()
string(JSON backend GET "${out}" driver_default_backend)
if(NOT backend STREQUAL llvm)
    message(FATAL_ERROR "Installation is not a native LLVM driver")
endif()
set(publisher "${PREFIX}.stages/publish-${id}")
if("${TARGET}" STREQUAL "windows-x86_64")
    string(APPEND publisher ".exe")
endif()
# CMake's POSIX NO_REPLACE implementation can use hard links, which cannot
# publish directories. Compile the small Wood publisher through the just-built
# normal CLI, then run it outside the staged directory after that CLI exits.
execute_process(COMMAND "${binary}" build "${data}/bootstrap/src/native_publish_main.wd"
    --backend=llvm -O0 -o "${publisher}" --native-home "${data}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300 ENCODING UTF-8)
file(WRITE "${PREFIX}.stages/publisher-${id}.log" "${out}${err}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Native install publisher build failed: ${status}: ${out}${err}")
endif()
execute_process(COMMAND "${publisher}" "${stage}" "${PREFIX}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60 ENCODING UTF-8)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Cannot publish native installation: ${status}: ${out}${err}")
endif()
message(STATUS "Installed LLVM-native normal CLI: ${PREFIX}; no C++ compiler/runtime used by native builds")
