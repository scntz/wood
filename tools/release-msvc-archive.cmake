cmake_minimum_required(VERSION 3.25)
foreach(required IN ITEMS LIBRARIAN OBJECT_DIR OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Supply ${required}")
    endif()
endforeach()
if(EXISTS "${OUTPUT}")
    message(FATAL_ERROR "Refusing existing archive: ${OUTPUT}")
endif()
# Repackage the supplied bridge objects with relative member names. For public
# builds, first strip debug metadata from private copies using llvm-objcopy
# --strip-debug: MSVC object-name records can contain absolute output paths.
# This script never alters the supplied object files or original build archive.
file(GLOB objects RELATIVE "${OBJECT_DIR}" "${OBJECT_DIR}/*.obj")
list(SORT objects)
list(LENGTH objects count)
if(NOT count EQUAL 3)
    message(FATAL_ERROR "Expected exactly the three native bridge objects")
endif()
get_filename_component(parent "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${parent}")
execute_process(COMMAND "${LIBRARIAN}" /nologo "/OUT:${OUTPUT}" ${objects}
    WORKING_DIRECTORY "${OBJECT_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Bridge archive packaging failed: ${out}${err}")
endif()
