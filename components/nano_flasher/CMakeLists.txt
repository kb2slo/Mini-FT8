# Nano firmware is staged by tools/stage_nano_firmware.sh from a
# `nano_companion` build (RFC 0001 §5.1). Absent that step this component
# still builds and links, but nano_flasher_flash_embedded() returns
# ESP_ERR_NOT_FOUND — CI and a fresh clone stay buildable without it.
set(NANO_FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target_firmware")
set(NANO_FW_BOOTLOADER "${NANO_FW_DIR}/bootloader.bin")
set(NANO_FW_PARTTABLE "${NANO_FW_DIR}/partition-table.bin")
set(NANO_FW_APP "${NANO_FW_DIR}/nano_companion.bin")

set(NANO_FLASHER_EMBED)
set(NANO_FLASHER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nano_target_firmware.h")

if(EXISTS "${NANO_FW_BOOTLOADER}" AND EXISTS "${NANO_FW_PARTTABLE}" AND EXISTS "${NANO_FW_APP}")
    set(NANO_FLASHER_EMBED
        "target_firmware/bootloader.bin"
        "target_firmware/partition-table.bin"
        "target_firmware/nano_companion.bin"
    )
    # IDF's EMBED_FILES names each symbol after the file's basename with
    # non-alnum characters replaced by `_`. Bind readable identifiers to
    # those exact linker symbols with GCC asm labels rather than guessing
    # spellings at the call site.
    file(WRITE "${NANO_FLASHER_HEADER}" "#pragma once\n#include <stdint.h>\n\n")
    file(APPEND "${NANO_FLASHER_HEADER}"
        "extern const uint8_t nano_bootloader_bin[] asm(\"_binary_bootloader_bin_start\");\n"
        "extern const uint8_t nano_bootloader_bin_end_[] asm(\"_binary_bootloader_bin_end\");\n"
        "#define nano_bootloader_bin_len ((uint32_t)(nano_bootloader_bin_end_ - nano_bootloader_bin))\n\n"
        "extern const uint8_t nano_partition_table_bin[] asm(\"_binary_partition_table_bin_start\");\n"
        "extern const uint8_t nano_partition_table_bin_end_[] asm(\"_binary_partition_table_bin_end\");\n"
        "#define nano_partition_table_bin_len ((uint32_t)(nano_partition_table_bin_end_ - nano_partition_table_bin))\n\n"
        "extern const uint8_t nano_companion_bin[] asm(\"_binary_nano_companion_bin_start\");\n"
        "extern const uint8_t nano_companion_bin_end_[] asm(\"_binary_nano_companion_bin_end\");\n"
        "#define nano_companion_bin_len ((uint32_t)(nano_companion_bin_end_ - nano_companion_bin))\n"
    )
else()
    message(STATUS "nano_flasher: no staged firmware in target_firmware/ — run tools/stage_nano_firmware.sh")
    if(EXISTS "${NANO_FLASHER_HEADER}")
        file(REMOVE "${NANO_FLASHER_HEADER}")
    endif()
endif()

idf_component_register(
    SRCS "nano_flasher.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    EMBED_FILES ${NANO_FLASHER_EMBED}
    REQUIRES esp-serial-flasher usb_host_cdc_acm
)

# A plain compiler define, not a `__has_include` file-existence check: ninja
# only invalidates an object file when its *compile command* changes, not
# when a header that didn't exist at compile time later appears. Staging
# firmware after an already-configured no-firmware build needs `idf.py
# reconfigure` either way (new EMBED_FILES), but this way the recompile
# actually happens instead of silently keeping the stale stub object.
if(NANO_FLASHER_EMBED)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NANO_FLASHER_HAVE_FIRMWARE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NANO_FLASHER_HAVE_FIRMWARE=0)
endif()
