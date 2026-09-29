# The knob's audio is a PCM5100A DAC and a PDM microphone on plain I2S. A board
# without that path builds audio_none.c instead, behind the same headers: see
# VFO_BOARD in the top-level CMakeLists.txt.
idf_build_get_property(board VFO_BOARD)
if(board STREQUAL "lcd185c")
    set(srcs "audio_none.c")
else()
    set(srcs "audio_out.c" "audio_in.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_i2s esp_driver_gpio
)
