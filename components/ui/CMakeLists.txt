# The chosen board's panel: see VFO_BOARD in the top-level CMakeLists.txt.
idf_build_get_property(board_panel VFO_BOARD_PANEL)
idf_component_register(
    SRCS "ui.c" "splash.c" "rfguru_logo.c" "font_mic_14.c"
    INCLUDE_DIRS "include"
    REQUIRES ${board_panel} hal_touch lvgl esp_lvgl_port net_prov
)
