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

set(SHA "unknown")
if(MINIFT8_GIT_SHA)
  set(_sha_in "${MINIFT8_GIT_SHA}")
elseif(DEFINED ENV{MINIFT8_GIT_SHA} AND NOT "$ENV{MINIFT8_GIT_SHA}" STREQUAL "")
  set(_sha_in "$ENV{MINIFT8_GIT_SHA}")
endif()
if(_sha_in)
  string(SUBSTRING "${_sha_in}" 0 7 SHA)
else()
  execute_process(
    COMMAND git -C "${GIT_DIR}" rev-parse --short=7 HEAD
    RESULT_VARIABLE sha_rv
    OUTPUT_VARIABLE sha_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(sha_rv EQUAL 0 AND sha_out)
    set(SHA "${sha_out}")
  endif()
endif()

set(DIRTY 0)
execute_process(
  COMMAND git -C "${GIT_DIR}" status --porcelain
  RESULT_VARIABLE st_rv
  OUTPUT_VARIABLE porcelain
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(st_rv EQUAL 0 AND porcelain)
  set(DIRTY 1)
endif()

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
file(WRITE "${OUT_DIR}/merged_bin_name.txt" "${MERGED_BIN_NAME}\n")
file(WRITE "${OUT_DIR}/build_identity.h" "\
#pragma once

#define MINIFT8_PRODUCT_VER \"${MINIFT8_PRODUCT_VER}\"
#define MINIFT8_GIT_SHA \"${SHA}\"
#define MINIFT8_GIT_DIRTY ${DIRTY}
#define MINIFT8_BUILD_KIND \"${KIND}\"
#define MINIFT8_UI_LINE \"${UI_LINE}\"
#define MINIFT8_MERGED_BIN_NAME \"${MERGED_BIN_NAME}\"
")
message(STATUS "Mini-FT8 identity: ${UI_LINE} -> ${MERGED_BIN_NAME}")
