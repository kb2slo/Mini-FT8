# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Separate idf.py project from the ADV app (mini_ft8). ESP-IDF locks target
# and sdkconfig per project, so the two chips cannot share one configure —
# see RFC 0001 §5.1. Pinned here so `idf.py build` works without a manual
# `set-target` step.
set(IDF_TARGET esp32c6)

# Exact git SHA + dirty flag, not ESP-IDF's default `git describe` (tag-
# relative, fuzzy) — via the same shared, override-aware logic
# tools/gen_build_identity.cmake uses for the ADV, not a second copy that
# can drift. Same repo, same commit, so "does the embedded sidekick.bin
# match what's on the Nano" is an exact string comparison (RFC 0001 §5.2b/
# §5.2c) — including under CI's MINIFT8_GIT_SHA pin, which a bare
# `git rev-parse HEAD` here would not otherwise honor. Must be set before
# project() — that's the only hook ESP-IDF's build system gives PROJECT_VER
# before it falls back to `git describe`.
set(GIT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
include("${CMAKE_CURRENT_LIST_DIR}/../tools/git_version.cmake")  # sets SHA, DIRTY
set(PROJECT_VER "${SHA}")
if(DIRTY)
    set(PROJECT_VER "${PROJECT_VER}-dirty")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sidekick)
