idf_component_register(
    SRCS "main.c" "wifi_prov.c" "host_link.c" "pairing_token.c" "pairing_http.c" "web_page.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server esp_app_format app_update porta_proto
             esp_driver_gpio esp_driver_uart esp_timer dns_server espressif__mdns

    # Real .html files rather than C string literals: editable, and checkable
    # by a formatter, validator or browser. EMBED_TXTFILES NUL-terminates, so
    # each arrives as a plain C string. Placeholders are {{name}}, not printf,
    # which is what lets the CSS say width:100% instead of width:100%%.
    EMBED_TXTFILES
        "web/viewer.html"
        "web/provision.html"
        "web/status.html"
        "web/saved.html"
        "web/error.html"
        "web/pairing.js"
)
