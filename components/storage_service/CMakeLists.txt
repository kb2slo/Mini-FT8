idf_component_register(
    SRCS "storage_service.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        adif
        driver
        esp_hw_support
        esp_partition
        espressif__esp_tinyusb
        fatfs
        freertos
        sdmmc
        wear_levelling
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=tud_msc_inquiry_cb" APPEND)
