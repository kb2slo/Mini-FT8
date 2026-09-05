idf_component_register(
    SRCS "main.cpp"
         "copy_to_sd.cpp"
         "file_list_worker.cpp"
         "station_save_worker.cpp"
         "autoseq.cpp"
         "audio_source.cpp"
         "core_api.cpp"
         "gps.cpp"
         "porta.cpp"
         "radio_control.cpp"
         "radio_control_qmx.cpp"
         "radio_control_qdx.cpp"
         "ft8_audio_pipeline.cpp"
         "stream_uac.cpp"
         "dds_q15.cpp"
         "resample.cpp"
         "station.cpp"
         "station_save_queue.cpp"
         "band_config.cpp"
         "usb_c_presence.cpp"
         "usb_c_presence_host.cpp"
         "adif.cpp"
         "qso_browse.cpp"
         "file_list.cpp"
         "file_list_queue.cpp"
         "cts_time.cpp"
         "cts_ble.cpp"
         "external_rtc.cpp"
         "storage_service.cpp"
    INCLUDE_DIRS "."
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib nano_flasher
             usb_host_uac usb_host_cdc_acm usb espressif__esp_tinyusb
             bt nvs_flash efuse esp_partition esp_hw_support
             driver esp_driver_i2c fatfs sdmmc wear_levelling
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
add_dependencies(${COMPONENT_LIB} minift8_build_identity)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_BINARY_DIR}/generated)
