idf_component_register(
    SRCS "band_config.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
