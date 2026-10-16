# Runs at build time (not configure) so EI_BUILD_SHA always tracks HEAD.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SRC}"
    OUTPUT_VARIABLE SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT SHA)
    set(SHA "unknown")
endif()
set(CONTENT "#define EI_BUILD_SHA \"${SHA}\"\n")
set(OLD "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD)
endif()
# only rewrite when the sha changed - avoids rebuilding dependents every build
if(NOT OLD STREQUAL CONTENT)
    file(WRITE "${OUT}" "${CONTENT}")
endif()
