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
    INCLUDE_DIRS "."
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib usb_host_uac usb_host_cdc_acm usb_c_presence storage_service external_rtc efuse station adif qso_browse file_list cts_time cts_ble band_config nano_flasher
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
add_dependencies(${COMPONENT_LIB} minift8_build_identity)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_BINARY_DIR}/generated)
