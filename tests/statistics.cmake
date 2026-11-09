foreach(level IN ITEMS -O0 -O2)
    execute_process(COMMAND "${WOOD}" "${SOURCE_DIR}/tests/statistics_edges.wd" "${level}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 120 ENCODING UTF-8)
    if(NOT "${status}" STREQUAL "0" OR NOT output STREQUAL "statistics edges ok\n")
        message(FATAL_ERROR "Statistics ${level} failed: ${status}\n${output}\n${error}")
    endif()
endforeach()
