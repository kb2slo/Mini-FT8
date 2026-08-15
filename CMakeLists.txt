# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# FT4 support — compile-time opt-in (default: ON).
#
# When ON, FT4 protocol code is included and the user can select FT8 or FT4
# at runtime via the Mode menu (Settings → Mode: FT8 | FT4). The choice is
# saved to Station.txt and takes effect on next boot.
#
# Set OFF to build a smaller FT8-only binary with no mode-switch menu item:
#   idf.py build -DENABLE_FT4=OFF
# ---------------------------------------------------------------------------
option(ENABLE_FT4 "Include FT4 protocol support and runtime mode switch" ON)

if(ENABLE_FT4)
    add_compile_definitions(ENABLE_FT4=1)
    message(STATUS "FT4 support: ON (runtime mode switch via menu)")
else()
    add_compile_definitions(ENABLE_FT4=0)
    message(STATUS "FT4 support: OFF (FT8-only build)")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini_ft8)

set(MINIFT8_BUILD_KIND "dev" CACHE STRING "Build kind: dev or rel")
set(MINIFT8_GEN_DIR "${CMAKE_BINARY_DIR}/generated")

# Configure-time identity so the first compile finds build_identity.h;
# the custom target refreshes SHA/dirty on every build.
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DGIT_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_DIR=${MINIFT8_GEN_DIR}
        -DMINIFT8_BUILD_KIND=${MINIFT8_BUILD_KIND}
        -P ${CMAKE_SOURCE_DIR}/tools/gen_build_identity.cmake
)
add_custom_target(minift8_build_identity
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MINIFT8_GEN_DIR}
    COMMAND ${CMAKE_COMMAND}
        -DGIT_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_DIR=${MINIFT8_GEN_DIR}
        -DMINIFT8_BUILD_KIND=${MINIFT8_BUILD_KIND}
        -P ${CMAKE_SOURCE_DIR}/tools/gen_build_identity.cmake
    COMMENT "Refreshing Mini-FT8 build identity"
    VERBATIM
    BYPRODUCTS
        ${MINIFT8_GEN_DIR}/build_identity.h
        ${MINIFT8_GEN_DIR}/merged_bin_name.txt
)

# Full merged image for the FATFS-only partition table. First boot formats
# the new 3 MB FATFS when needed. Also copy a hash-first name for Launcher.
add_custom_command(TARGET app POST_BUILD
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin -o "MiniFT8_Merged_Auto.bin" @flash_args
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tools/copy_named_merged_bin.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating merged binaries MiniFT8_Merged_Auto.bin and hash-first name"
)
