cmake_minimum_required(VERSION 3.25)
# Native development/candidate packages retain their declared qualification
# boundary. Packaging does not establish Core parity or clean-host support.
if(NOT DEFINED SOURCE_DATE_EPOCH)
    message(FATAL_ERROR "Supply -DSOURCE_DATE_EPOCH=<source commit Unix timestamp> for reproducible native packages")
endif()
if(NOT OUTPUT MATCHES "[.]tar[.]gz$")
    message(FATAL_ERROR "Native reproducible packages require an OUTPUT ending in .tar.gz")
endif()
set(PROFILE Native)
include("${CMAKE_CURRENT_LIST_DIR}/package-selfhost.cmake")
