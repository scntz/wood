cmake_minimum_required(VERSION 3.25)
# Separate CTest profiles can run concurrently. Their large staged compiler
# builds share one lock so GCC and sanitizer bootstrap jobs do not overlap.
if(NOT SUITE MATCHES "^bootstrap_(compiler|project_trust)$" OR
        NOT DEFINED QUALIFICATION_NATIVE_LOCK)
    message(FATAL_ERROR "Invalid serialized bootstrap invocation")
endif()
message(STATUS "Acquiring native bootstrap lock for ${SUITE}")
file(LOCK "${QUALIFICATION_NATIVE_LOCK}" GUARD PROCESS TIMEOUT 5400 RESULT_VARIABLE status)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Native bootstrap lock failed: ${status}")
endif()
include("${SOURCE_DIR}/tests/${SUITE}.cmake")
