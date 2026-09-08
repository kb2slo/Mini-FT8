idf_component_register(
    SRCS "main.cpp"
         "storage/copy_to_sd.cpp"
         "storage/file_list_worker.cpp"
         "storage/station_save_worker.cpp"
         "autoseq.cpp"
         "audio/audio_source.cpp"
         "time/gps.cpp"
         "porta.cpp"
         "radio/radio_control.cpp"
         "radio/radio_control_qmx.cpp"
         "radio/radio_control_qdx.cpp"
         "audio/ft8_audio_pipeline.cpp"
         "audio/stream_uac.cpp"
         "audio/dds_q15.cpp"
         "audio/resample.cpp"
         "storage/station.cpp"
         "storage/station_save_queue.cpp"
         "band_config.cpp"
         "menu_model.cpp"
         "screen_model.cpp"
         "usb_c_presence.cpp"
         "usb_c_presence_host.cpp"
         "storage/adif.cpp"
         "storage/qso_browse.cpp"
         "storage/file_list.cpp"
         "storage/file_list_queue.cpp"
         "time/cts_time.cpp"
         "time/cts_ble.cpp"
         "time/external_rtc.cpp"
         "storage/storage_service.cpp"
    INCLUDE_DIRS "." "radio" "audio" "storage" "time"
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib nano_flasher
             usb_host_uac usb_host_cdc_acm usb espressif__esp_tinyusb
             bt nvs_flash efuse esp_partition esp_hw_support
             driver esp_driver_i2c fatfs sdmmc wear_levelling
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
add_dependencies(${COMPONENT_LIB} minift8_build_identity)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_BINARY_DIR}/generated)
