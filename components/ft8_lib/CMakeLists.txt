# IDF wrapper around the pinned ft8_lib fork (vendor/).
# Compile our monitor (static BSS arenas) and fft_wrapper; do not compile
# vendor/common/monitor.c (malloc). Protocol sources come from the pin.
idf_component_register(SRCS
    vendor/ft8/decode.c
    vendor/ft8/message.c
    vendor/ft8/text.c
    vendor/ft8/encode.c
    vendor/ft8/constants.c
    vendor/ft8/crc.c
    vendor/ft8/ldpc.c
    vendor/fft/kiss_fft.c
    vendor/fft/kiss_fftr.c
    common/monitor.c
    common/fft_wrapper.c
    INCLUDE_DIRS "." "vendor"
)

# Relax warnings that trigger -Werror in IDF. Vendor tree is not ours to restyle.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-const-variable
    -Wno-format
)
