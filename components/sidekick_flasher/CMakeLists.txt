# The sidekick payload this component embeds is built *by this build*, not
# staged into it by hand (B52).
#
# It used to be three manual steps in order -- build the sidekick, run
# tools/stage_sidekick_firmware.sh, build the ADV -- with nothing checking that
# the first two had happened. Skipping them embedded an old payload, which the
# ADV then *installed* onto the companion on the next field-flash: a silent
# downgrade. Worse, the version-compare machinery meant to catch drift then
# reported a mismatch that was our own build's fault rather than a field fact.
#
# EMBED_FILES fixes the file *list* at configure time but reads the file
# *contents* at build time, so a build rule producing them is enough; the list
# just has to be unconditional. ninja orders it because the custom command's
# OUTPUTs are exactly the paths EMBED_FILES names.
set(NANO_FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target_firmware")
set(NANO_FW_BOOTLOADER "${NANO_FW_DIR}/bootloader.bin")
set(NANO_FW_PARTTABLE "${NANO_FW_DIR}/partition-table.bin")
set(NANO_FW_APP "${NANO_FW_DIR}/sidekick.bin")
set(NANO_FW_OTADATA "${NANO_FW_DIR}/ota_data_initial.bin")

# IDF's EMBED_FILES names each symbol after the file's basename with non-alnum
# characters replaced by `_`. Bind readable identifiers to those exact linker
# symbols with GCC asm labels rather than guessing spellings at the call site.
set(SIDEKICK_FLASHER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nano_target_firmware.h")
file(WRITE "${SIDEKICK_FLASHER_HEADER}" "#pragma once\n#include <stdint.h>\n\n")
file(APPEND "${SIDEKICK_FLASHER_HEADER}"
    "extern const uint8_t sidekick_bootloader_bin[] asm(\"_binary_bootloader_bin_start\");\n"
    "extern const uint8_t sidekick_bootloader_bin_end_[] asm(\"_binary_bootloader_bin_end\");\n"
    "#define sidekick_bootloader_bin_len ((uint32_t)(sidekick_bootloader_bin_end_ - sidekick_bootloader_bin))\n\n"
    "extern const uint8_t sidekick_partition_table_bin[] asm(\"_binary_partition_table_bin_start\");\n"
    "extern const uint8_t sidekick_partition_table_bin_end_[] asm(\"_binary_partition_table_bin_end\");\n"
    "#define sidekick_partition_table_bin_len ((uint32_t)(sidekick_partition_table_bin_end_ - sidekick_partition_table_bin))\n\n"
    "extern const uint8_t sidekick_otadata_bin[] asm(\"_binary_ota_data_initial_bin_start\");\n"
    "extern const uint8_t sidekick_otadata_bin_end_[] asm(\"_binary_ota_data_initial_bin_end\");\n"
    "#define sidekick_otadata_bin_len ((uint32_t)(sidekick_otadata_bin_end_ - sidekick_otadata_bin))\n\n"
    "extern const uint8_t sidekick_bin[] asm(\"_binary_sidekick_bin_start\");\n"
    "extern const uint8_t sidekick_bin_end_[] asm(\"_binary_sidekick_bin_end\");\n"
    "#define sidekick_bin_len ((uint32_t)(sidekick_bin_end_ - sidekick_bin))\n"
)

idf_component_register(
    SRCS "sidekick_flasher.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    EMBED_FILES
        "target_firmware/bootloader.bin"
        "target_firmware/partition-table.bin"
        "target_firmware/sidekick.bin"
        "target_firmware/ota_data_initial.bin"
    REQUIRES esp-serial-flasher usb_host_cdc_acm esp_driver_uart
)

# Everything below runs only in the real configure pass. IDF re-executes this
# file in script mode to gather REQUIRES, where add_custom_command and
# add_dependencies do not belong -- putting them before the register call broke
# IDF's generated properties file with a syntax error rather than failing
# anywhere that named the cause.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(SIDEKICK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../sidekick")

    # Anything that changes the payload. Globbed rather than listed, because a
    # forgotten new file is exactly the staleness this rule exists to prevent.
    file(GLOB_RECURSE SIDEKICK_SOURCES
        "${SIDEKICK_DIR}/main/*"
        "${SIDEKICK_DIR}/components/*"
        "${SIDEKICK_DIR}/CMakeLists.txt"
        "${SIDEKICK_DIR}/partitions.csv"
        "${SIDEKICK_DIR}/sdkconfig.defaults")

    # idf.py rather than ExternalProject: the sidekick is a separate idf.py
    # project precisely because ESP-IDF locks target and sdkconfig per project
    # (RFC 0001 §5.1), so nesting its CMake inside this one fights that on
    # purpose. Treating it as an opaque external build is what a Makefile would
    # do, and keeps the two projects as independent as they were designed to be.
    # idf.py is incremental, so an unchanged sidekick costs a no-op.
    add_custom_command(
        OUTPUT "${NANO_FW_APP}" "${NANO_FW_BOOTLOADER}" "${NANO_FW_PARTTABLE}" "${NANO_FW_OTADATA}"
        COMMAND idf.py -C "${SIDEKICK_DIR}" build
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NANO_FW_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${SIDEKICK_DIR}/build/sidekick.bin" "${NANO_FW_APP}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${SIDEKICK_DIR}/build/bootloader/bootloader.bin" "${NANO_FW_BOOTLOADER}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${SIDEKICK_DIR}/build/partition_table/partition-table.bin" "${NANO_FW_PARTTABLE}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${SIDEKICK_DIR}/build/ota_data_initial.bin" "${NANO_FW_OTADATA}"
        DEPENDS ${SIDEKICK_SOURCES}
        COMMENT "Building sidekick firmware for embedding"
        VERBATIM)

    add_custom_target(sidekick_payload
        DEPENDS "${NANO_FW_APP}" "${NANO_FW_BOOTLOADER}" "${NANO_FW_PARTTABLE}" "${NANO_FW_OTADATA}")
    add_dependencies(${COMPONENT_LIB} sidekick_payload)
endif()

# The payload is now always produced, so the no-firmware stub path in
# sidekick_flasher.cpp is unreachable here. It stays compiled out rather than
# deleted: it is the contract for any build that does not embed one, and
# removing it would turn that into a link error instead of a clear
# ESP_ERR_NOT_FOUND.
target_compile_definitions(${COMPONENT_LIB} PRIVATE SIDEKICK_FLASHER_HAVE_FIRMWARE=1)
