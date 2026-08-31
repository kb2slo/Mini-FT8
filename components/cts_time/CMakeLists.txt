idf_component_register(
    SRCS "cts_time.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
