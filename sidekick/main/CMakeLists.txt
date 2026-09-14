# web/ is the single source of truth for seed assets (RFC 0004 §3).
# Drop a file in web/ → it is embedded, hydrated onto LittleFS, and served at
# /<filename> by the static handler. No C table to edit. Templated routes
# (/, /log, …) still need handlers because their URLs are not the filenames.

set(WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/web")

# IDF processes this file once in script mode (requirements) where
# CONFIGURE_DEPENDS is illegal; again for the real project build where it is
# what makes "drop a file in web/" pick up without editing this list.
if(CMAKE_BUILD_EARLY_EXPANSION)
    file(GLOB WEB_SEED_NAMES RELATIVE "${WEB_DIR}" "${WEB_DIR}/*")
else()
    file(GLOB WEB_SEED_NAMES CONFIGURE_DEPENDS RELATIVE "${WEB_DIR}" "${WEB_DIR}/*")
endif()
list(SORT WEB_SEED_NAMES)

set(WEB_EMBED_TXTFILES "")
set(WEB_SEED_EXTERNS "")
set(WEB_SEED_ENTRIES "")
set(WEB_SEED_COUNT 0)

foreach(name IN LISTS WEB_SEED_NAMES)
    if(IS_DIRECTORY "${WEB_DIR}/${name}")
        # skip directories
    elseif(name MATCHES "^\\." OR name MATCHES "~$")
        # skip editor junk
    else()
        list(APPEND WEB_EMBED_TXTFILES "web/${name}")
        string(MAKE_C_IDENTIFIER "${name}" sym)
        file(SIZE "${WEB_DIR}/${name}" fsize)

        string(APPEND WEB_SEED_EXTERNS
            "extern const char _binary_${sym}_start[] asm(\"_binary_${sym}_start\");\n")
        string(APPEND WEB_SEED_ENTRIES
            "    {\"${name}\", _binary_${sym}_start, ${fsize}u},\n")
        math(EXPR WEB_SEED_COUNT "${WEB_SEED_COUNT} + 1")
    endif()
endforeach()

if(WEB_SEED_COUNT EQUAL 0)
    message(FATAL_ERROR "sidekick/main/web/ has no seed files to embed")
endif()

set(WEB_SEED_GEN "${CMAKE_CURRENT_BINARY_DIR}/web_seed_table.c")
set(WEB_SEED_SRCS "main.c" "wifi_prov.c" "host_link.c" "pairing_token.c" "pairing_http.c"
                  "web_page.c" "web_fs.c")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(WRITE "${WEB_SEED_GEN}" "\
/* Generated from main/web/ — do not edit. */\n\
#include \"web_seed_table.h\"\n\
\n\
${WEB_SEED_EXTERNS}\n\
const web_seed_file_t web_seed_files[] = {\n\
${WEB_SEED_ENTRIES}\
};\n\
\n\
const size_t web_seed_files_count = ${WEB_SEED_COUNT};\n\
")
    list(APPEND WEB_SEED_SRCS "${WEB_SEED_GEN}")
endif()

idf_component_register(
    SRCS ${WEB_SEED_SRCS}
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server esp_app_format app_update porta_proto
             esp_driver_gpio esp_driver_uart esp_timer dns_server espressif__mdns joltwallet__littlefs

    # Real files under web/, NUL-terminated by EMBED_TXTFILES. Placeholders are
    # {{name}}, not printf — CSS can say width:100%. The file list is the glob
    # above, not a second handwritten list here.
    EMBED_TXTFILES ${WEB_EMBED_TXTFILES}
)
