idf_component_register(
    SRCS "cts_ble.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash cts_time
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
