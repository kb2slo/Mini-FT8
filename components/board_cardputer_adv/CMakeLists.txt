idf_component_register(
    SRCS "board_i2c.cpp" "board_audio.cpp" "board_power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2s esp_driver_i2c esp_adc esp_codec_dev esp_timer
)
