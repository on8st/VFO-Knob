# One bring-up per board, behind the same board.h: see VFO_BOARD in the
# top-level CMakeLists.txt.
idf_build_get_property(board VFO_BOARD)
if(board STREQUAL "lcd185c")
    set(srcs "board_lcd185c.c")
else()
    set(srcs "board_init.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio
)
