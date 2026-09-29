/* ST77916 QSPI panel, 360x360 RGB565, plus the LEDC backlight -- the lcd185c
 * board's display, behind the same interface as the knob's SH8601 panel
 * (components/panel/include/panel.h), so the face above it does not know
 * which board it is on.
 */
#ifndef VFO_PANEL_H
#define VFO_PANEL_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/* The core that drives the display: see components/panel/include/panel.h for
 * why the SPI interrupt and the LVGL task must share one. */
#define PANEL_CORE 1

esp_err_t panel_init(void);

/* Handle for LVGL to flush into. */
esp_lcd_panel_handle_t panel_handle(void);

/* The panel IO handle, for esp_lvgl_port's transfer-done callback. */
esp_lcd_panel_io_handle_t panel_io_handle(void);

/* 0..255. LEDC 8-bit on the backlight pin. */
void panel_set_brightness(uint8_t duty);

/* The same M4 acceptance test as the knob's: solid colours, a 1 px ring at
 * r=179, a backlight ramp. */
esp_err_t panel_selftest(void);

#endif /* VFO_PANEL_H */
