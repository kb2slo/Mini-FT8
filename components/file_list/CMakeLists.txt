idf_component_register(
    SRCS "file_list.cpp" "file_list_queue.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES qso_browse
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
