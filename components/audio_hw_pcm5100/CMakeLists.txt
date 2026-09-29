# The knob's audio hardware, beneath components/audio. Only one audio_hw_*
# component is built: see VFO_BOARD in the top-level CMakeLists.txt.
idf_component_register(
    SRCS "audio_hw_pcm5100.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_i2s esp_driver_gpio
)
