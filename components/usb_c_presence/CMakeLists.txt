idf_component_register(
    SRCS "usb_c_presence.cpp" "usb_c_presence_host.cpp"
    INCLUDE_DIRS "include"
    REQUIRES usb
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
