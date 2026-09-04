# Generate build_identity.h and merged_bin_name.txt from git.
# Required: GIT_DIR, OUT_DIR
# Optional: MINIFT8_BUILD_KIND (default dev)

if(NOT GIT_DIR OR NOT OUT_DIR)
  message(FATAL_ERROR "gen_build_identity.cmake needs GIT_DIR and OUT_DIR")
endif()

if(NOT MINIFT8_BUILD_KIND)
  set(MINIFT8_BUILD_KIND "dev")
endif()

set(MINIFT8_PRODUCT_VER "2.1d")

include("${CMAKE_CURRENT_LIST_DIR}/git_version.cmake")  # sets SHA, DIRTY

set(KIND "${MINIFT8_BUILD_KIND}")
if(KIND STREQUAL "release" OR KIND STREQUAL "rel")
  set(KIND "rel")
  set(KIND_SUFFIX "")
else()
  set(KIND "dev")
  set(KIND_SUFFIX "-dev")
endif()

if(DIRTY)
  set(DIRTY_INFIX "-dirty")
  set(DIRTY_MARK "*")
else()
  set(DIRTY_INFIX "")
  set(DIRTY_MARK "")
endif()

# Hash first so M5Launcher's truncated names still show the unique bit.
set(MERGED_BIN_NAME "${SHA}${DIRTY_INFIX}-minift8${KIND_SUFFIX}.bin")
set(UI_LINE "${KIND} ${SHA}${DIRTY_MARK}")

file(MAKE_DIRECTORY "${OUT_DIR}")

# minift8_build_identity is an add_custom_target (main/CMakeLists.txt depends
# on it), so it reruns unconditionally on every build by design — that's the
# only way to catch a newly-dirtied tree. But a plain file(WRITE) rewrites
# the destination's mtime even when the content is byte-identical, and
# main.cpp #includes build_identity.h — so an unconditional WRITE meant
# ninja recompiled + relinked main.cpp on every single `idf.py build`, even
# with zero source changes, every time git state (SHA/dirty) happened to be
# unchanged. Write to a .tmp file and copy_if_different so the real output's
# mtime only moves when the content actually does.
file(WRITE "${OUT_DIR}/merged_bin_name.txt.tmp" "${MERGED_BIN_NAME}\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${OUT_DIR}/merged_bin_name.txt.tmp" "${OUT_DIR}/merged_bin_name.txt")
file(REMOVE "${OUT_DIR}/merged_bin_name.txt.tmp")

file(WRITE "${OUT_DIR}/build_identity.h.tmp" "\
#pragma once

#define MINIFT8_PRODUCT_VER \"${MINIFT8_PRODUCT_VER}\"
#define MINIFT8_GIT_SHA \"${SHA}\"
#define MINIFT8_GIT_DIRTY ${DIRTY}
#define MINIFT8_BUILD_KIND \"${KIND}\"
#define MINIFT8_UI_LINE \"${UI_LINE}\"
#define MINIFT8_MERGED_BIN_NAME \"${MERGED_BIN_NAME}\"
")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${OUT_DIR}/build_identity.h.tmp" "${OUT_DIR}/build_identity.h")
file(REMOVE "${OUT_DIR}/build_identity.h.tmp")

message(STATUS "Mini-FT8 identity: ${UI_LINE} -> ${MERGED_BIN_NAME}")
