# Writes BuildData.json (VERSION, REVISION, BUILD_DATE) in the same form as the
# Mac's generate_builddata.sh and Windows' generate_build_version.py. Runs as a
# build step (cmake -P) so every build, not just a reconfigure, gets the
# current revision and time.
#
# Inputs: APP_VERSION (may be empty), SRC_DIR, IN_FILE, OUT_FILE.

find_package(Git QUIET)

if(NOT APP_VERSION AND GIT_FOUND)
    # Dev build: versioned from the latest tag, as on Mac and Windows.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE APP_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX REPLACE "^[vV]([0-9])" "\\1" APP_VERSION "${APP_VERSION}")
endif()
if(NOT APP_VERSION)
    set(APP_VERSION "0.0.0")
endif()

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_REVISION)
    set(GIT_REVISION "unknown")
endif()

string(TIMESTAMP BUILD_DATE "%a %b %d %H:%M:%S UTC %Y" UTC)

configure_file(${IN_FILE} ${OUT_FILE} @ONLY)
