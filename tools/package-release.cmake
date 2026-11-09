# Verified Core packaging shares the candidate packager's hash/extraction and
# no-overwrite checks. Use package-selfhost.cmake for the separate candidate.
set(PROFILE Core)
include("${CMAKE_CURRENT_LIST_DIR}/package-selfhost.cmake")
