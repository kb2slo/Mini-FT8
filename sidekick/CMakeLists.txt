# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Separate idf.py project from the ADV app (mini_ft8). ESP-IDF locks target
# and sdkconfig per project, so the two chips cannot share one configure —
# see RFC 0001 §5.1. Pinned here so `idf.py build` works without a manual
# `set-target` step.
set(IDF_TARGET esp32c6)

# Exact git SHA + dirty flag, not ESP-IDF's default `git describe` (tag-
# relative, fuzzy). Matches the ADV's own tools/gen_build_identity.cmake
# scheme — same repo, same commit, so "does the embedded sidekick.bin match
# what's on the Nano" is an exact string comparison (RFC 0001 §5.2b). Must
# be set before project() — that's the only hook ESP-IDF's build system
# gives PROJECT_VER before it falls back to `git describe`.
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" rev-parse --short=7 HEAD
    RESULT_VARIABLE _sidekick_sha_rv
    OUTPUT_VARIABLE _sidekick_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _sidekick_sha_rv EQUAL 0 OR NOT _sidekick_sha)
    set(_sidekick_sha "unknown")
endif()
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" status --porcelain
    RESULT_VARIABLE _sidekick_dirty_rv
    OUTPUT_VARIABLE _sidekick_porcelain
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
set(PROJECT_VER "${_sidekick_sha}")
if(_sidekick_dirty_rv EQUAL 0 AND _sidekick_porcelain)
    set(PROJECT_VER "${PROJECT_VER}-dirty")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sidekick)
