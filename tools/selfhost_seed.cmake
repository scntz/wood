cmake_minimum_required(VERSION 3.25)
# Preserve the emitter's no-overwrite contract and all prior generated evidence.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(stage "${OUTPUT}.stages/${run_id}.cpp")
get_filename_component(directory "${stage}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
execute_process(COMMAND "${EMITTER}" "${STDLIB}" "${RUNTIME}" "${INPUT}" "${stage}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Preserved Wood seed failed (${status}): ${out}${err}")
endif()
configure_file("${stage}" "${OUTPUT}" COPYONLY)
