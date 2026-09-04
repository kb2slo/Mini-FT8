# Shared git SHA + dirty-flag computation, honoring the same MINIFT8_GIT_SHA
# override (CMake var or env var) CI uses to pin the reported SHA to the
# actual PR/commit head rather than trusting a bare `git rev-parse HEAD` —
# CI checks out a GitHub-synthesized merge commit for a PR, which is a
# different SHA than github.event.pull_request.head.sha, so a bare
# rev-parse there would silently disagree with the pinned value.
#
# Used by both tools/gen_build_identity.cmake (ADV) and
# sidekick/CMakeLists.txt: RFC 0001 §5.2b/§5.2c need the two independently-
# built firmwares' version strings to be exactly comparable — one shared
# implementation, not two copies that can drift apart.
#
# Input:  GIT_DIR (required), MINIFT8_GIT_SHA (optional override)
# Output: SHA, DIRTY (0 or 1) — set in the caller's scope

if(NOT GIT_DIR)
  message(FATAL_ERROR "git_version.cmake needs GIT_DIR")
endif()

set(SHA "unknown")
set(_sha_in "")
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
