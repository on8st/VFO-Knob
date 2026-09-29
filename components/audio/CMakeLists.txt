# The audio logic, shared by every board, over the board's own audio hardware
# component (audio_hw.h): see VFO_BOARD in the top-level CMakeLists.txt.
idf_build_get_property(board_audio_hw VFO_BOARD_AUDIO_HW)
idf_component_register(
    SRCS "audio_out.c" "audio_in.c"
    INCLUDE_DIRS "include"
    REQUIRES ${board_audio_hw} esp_driver_i2s
)
