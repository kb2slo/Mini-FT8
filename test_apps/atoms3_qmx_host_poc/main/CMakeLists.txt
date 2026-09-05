idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES usb usb_host_cdc_acm
)
