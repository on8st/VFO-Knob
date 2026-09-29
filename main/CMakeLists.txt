# The chosen radio's client and board's panel: see VFO_RADIO and VFO_BOARD in
# the top-level CMakeLists.txt.
idf_build_get_property(radio_client VFO_RADIO_CLIENT)
idf_build_get_property(board_panel VFO_BOARD_PANEL)
idf_component_register(
    SRCS "app_main.c" "gpio_scan.c"
    INCLUDE_DIRS "."
    REQUIRES board netlog webcfg ota vfo_core hal_haptic hal_encoder ${board_panel} hal_touch ui net_prov radio ${radio_client} audio usb_net esp_psram driver esp_timer esp_driver_usb_serial_jtag espcoredump
)
