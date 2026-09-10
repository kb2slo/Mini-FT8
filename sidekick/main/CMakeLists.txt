idf_component_register(
    SRCS "main.c" "wifi_prov.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server esp_app_format app_update
             esp_driver_gpio esp_driver_uart esp_timer dns_server espressif__mdns
)
