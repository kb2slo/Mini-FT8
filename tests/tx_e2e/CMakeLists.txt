cmake_minimum_required(VERSION 3.16)
project(tx_e2e C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

if(MSVC)
    message(FATAL_ERROR "MSVC is unsupported due to its lack of C99 VLA support. Please use MinGW/GCC or Clang.")
endif()

# Compiler flags
# Force SSE2 math on 32-bit x86 GCC (MinGW) to avoid x87 excess precision.
# Without this, 6000 * 0.160f is computed in 80-bit extended, giving 959.999...
# which truncates to 959 (odd) and breaks kiss_fftr's even-nfft requirement.
# SSE2 rounds the float32 product to 960.0f first, matching MSVC x64 behaviour.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse -msse2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpmath=sse -msse2")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-format")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")

# Protocol/FFT from the pinned fork; monitor and fft_wrapper stay Mini-FT8.
set(FT8_LIB_DIR ${CMAKE_SOURCE_DIR}/../../components/ft8_lib)
set(FT8_LIB_SOURCES
    ${FT8_LIB_DIR}/vendor/ft8/decode.c
    ${FT8_LIB_DIR}/vendor/ft8/message.c
    ${FT8_LIB_DIR}/vendor/ft8/text.c
    ${FT8_LIB_DIR}/vendor/ft8/encode.c
    ${FT8_LIB_DIR}/vendor/ft8/constants.c
    ${FT8_LIB_DIR}/vendor/ft8/crc.c
    ${FT8_LIB_DIR}/vendor/ft8/ldpc.c
    ${FT8_LIB_DIR}/vendor/fft/kiss_fft.c
    ${FT8_LIB_DIR}/vendor/fft/kiss_fftr.c
    ${FT8_LIB_DIR}/common/monitor.c
    ${FT8_LIB_DIR}/common/fft_wrapper.c
)
if(WIN32)
    list(APPEND FT8_LIB_SOURCES ${FT8_LIB_DIR}/common/stpcpy_compat.c)
endif()

# Create ft8_lib static library
add_library(ft8_lib STATIC ${FT8_LIB_SOURCES})
target_include_directories(ft8_lib PUBLIC ${FT8_LIB_DIR} ${FT8_LIB_DIR}/vendor)
target_compile_options(ft8_lib PRIVATE
    -Wno-unused-const-variable
    -Wno-format
    -Wno-unused-variable
)

# Link against math library (only on non-Windows systems)
if(NOT WIN32)
    target_link_libraries(ft8_lib PUBLIC m)
endif()

# L1 Encoder Test
add_executable(test_l1 test_l1_encoder.cpp decode_helper.cpp synth.c)
target_include_directories(test_l1 PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_l1 PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(test_l1 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l1 PRIVATE ft8_lib m)
endif()



# L2 State Machine Test
add_executable(test_l2 test_l2_state_machine.cpp tx_state_machine.cpp decode_helper.cpp)
target_include_directories(test_l2 PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/../../main)
if(WIN32)
    target_link_libraries(test_l2 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l2 PRIVATE ft8_lib m)
endif()

# Golden WAV generator
add_executable(gen_golden gen_golden.cpp decode_helper.cpp synth.c)
target_include_directories(gen_golden PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(gen_golden PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(gen_golden PRIVATE ft8_lib)
else()
    target_link_libraries(gen_golden PRIVATE ft8_lib m)
endif()

# L3 TX Poll-Timing test
add_executable(test_l3 test_l3_tx_poll_timing.cpp decode_helper.cpp synth.c)
target_include_directories(test_l3 PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_l3 PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(test_l3 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l3 PRIVATE ft8_lib m)
endif()

# Golden RX test
add_executable(test_golden_rx test_golden_rx.cpp decode_helper.cpp)
target_include_directories(test_golden_rx PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(test_golden_rx PRIVATE "GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/golden\"")
target_compile_options(test_golden_rx PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(test_golden_rx PRIVATE ft8_lib)
else()
    target_link_libraries(test_golden_rx PRIVATE ft8_lib m)
endif()

# TA command format test (firmware header; no ft8_lib)
add_executable(test_ta_format test_ta_format.cpp)
target_include_directories(test_ta_format PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/../../main)
if(NOT WIN32)
    target_link_libraries(test_ta_format PRIVATE m)
endif()

# L4 TX Timer-Isolation test
add_executable(test_l4 test_l4_tx_timer_isolation.cpp decode_helper.cpp)
target_include_directories(test_l4 PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_l4 PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(test_l4 PRIVATE ft8_lib)
else()
    target_link_libraries(test_l4 PRIVATE ft8_lib m)
endif()

# WAV diagnostic tool
add_executable(test_wav_diag test_wav_diag.cpp decode_helper.cpp)
target_include_directories(test_wav_diag PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_wav_diag PRIVATE -Wno-format)
if(WIN32)
    target_link_libraries(test_wav_diag PRIVATE ft8_lib)
else()
    target_link_libraries(test_wav_diag PRIVATE ft8_lib m)
endif()

# Enable testing
enable_testing()

add_test(NAME l1_encoder         COMMAND test_l1)
add_test(NAME l2_state_machine   COMMAND test_l2)
add_test(NAME l3_poll_timing     COMMAND test_l3)
add_test(NAME l4_timer_isolation COMMAND test_l4)
add_test(NAME golden_rx          COMMAND test_golden_rx)
add_test(NAME ta_format          COMMAND test_ta_format)
