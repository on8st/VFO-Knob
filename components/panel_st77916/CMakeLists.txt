# The lcd185c board's panel. Provides the same panel.h as components/panel,
# and only one of the two is ever built: see VFO_BOARD in the top-level
# CMakeLists.txt.
idf_component_register(
    SRCS "panel_st77916.c" "st77916_init_cmds.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_lcd esp_driver_spi esp_driver_ledc esp_lcd_st77916
)
