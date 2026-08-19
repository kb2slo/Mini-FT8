idf_component_register(
    SRCS "qso_browse.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
