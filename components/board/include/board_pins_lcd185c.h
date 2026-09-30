/* Waveshare ESP32-S3-Touch-LCD-1.85C pin map (VFO_BOARD=lcd185c), including
 * the "BOX" enclosure with its speaker.
 *
 * Only board_pins_<board>.h files may contain GPIO numbers; include
 * board_pins.h, never this file directly.
 *
 * Taken from Waveshare's own ESP32-S3-Touch-LCD-1.85C-Test sources
 * (github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C), and checked against a
 * unit: its factory image reads register 0x04 as 00 02 7F 7F and reports the
 * CST816 at 0x15.
 */
#ifndef BOARD_PINS_LCD185C_H
#define BOARD_PINS_LCD185C_H

#include "driver/gpio.h"

/* What this board has. No knob and no haptic motor: tuning and PTT are both
 * on the glass, so they have to be told apart by gesture, not by place. */
#define BOARD_NAME              "lcd185c"
#define BOARD_HAS_ENCODER       0
#define BOARD_HAS_HAPTIC        0
#define BOARD_HAS_AUDIO_MUX     0
#define BOARD_HAS_SECOND_CHIP   0

/* --- display: ST77916 over QSPI ------------------------------------------
 * The same 360x360 as the knob, so the face carries over as it is. Reset is
 * not a GPIO but EXIO2 on the TCA9554 expander, pulsed by board_init(). Two
 * panel revisions exist, told apart by register 0x04; see panel_st77916. */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_H_RES         360
#define BOARD_LCD_V_RES         360
#define BOARD_PIN_LCD_CS        GPIO_NUM_21
#define BOARD_PIN_LCD_PCLK      GPIO_NUM_40
#define BOARD_PIN_LCD_DATA0     GPIO_NUM_46
#define BOARD_PIN_LCD_DATA1     GPIO_NUM_45
#define BOARD_PIN_LCD_DATA2     GPIO_NUM_42
#define BOARD_PIN_LCD_DATA3     GPIO_NUM_41
#define BOARD_PIN_LCD_TE        GPIO_NUM_18
#define BOARD_PIN_LCD_RST       GPIO_NUM_NC     /* EXIO2 */
#define BOARD_PIN_LCD_BL        GPIO_NUM_5

/* Upright with the USB cable pointing down: the panel's own orientation. */
#define BOARD_UI_ROTATION       0       /* quarter turns */

/* Vendor runs 80 MHz; the knob's 40 MHz is the known-safe figure for
 * GPIO-matrix QSPI, and a digit update costs next to nothing either way. */
#define BOARD_LCD_PCLK_HZ       (40 * 1000 * 1000)

/* --- shared I2C bus ------------------------------------------------------
 * Busier than the knob's: the expander, touch, RTC, IMU and both audio codecs
 * all live here. Touch is still the PTT input, so every other transaction
 * keeps a finite timeout. */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_PIN_I2C_SDA       GPIO_NUM_11
#define BOARD_PIN_I2C_SCL       GPIO_NUM_10
#define BOARD_I2C_HZ            400000

#define BOARD_I2C_ADDR_TOUCH    0x15    /* CST816   */
#define BOARD_I2C_ADDR_EXPANDER 0x20    /* TCA9554  */
#define BOARD_I2C_ADDR_RTC      0x51    /* PCF85063 */
#define BOARD_I2C_ADDR_DAC      0x18    /* ES8311, to the speaker */
#define BOARD_I2C_ADDR_ADC      0x40    /* ES7210, from the microphones */

/* The TCA9554's pins, numbered from 1 as Waveshare's schematic does. */
#define BOARD_EXIO_TOUCH_RST    1
#define BOARD_EXIO_LCD_RST      2
#define BOARD_EXIO_SD_CS        3

/* The CST816's INT is on GPIO4, but it is deliberately left unused. Given an
 * interrupt pin, esp_lvgl_port reads touch only when that pin fires, and on
 * this board it never did: the dial drew and nothing on it responded. Polled
 * every TOUCH_POLL_MS it works, and polling is what the knob's latency
 * figures assume anyway. */
#define BOARD_PIN_TOUCH_RST     GPIO_NUM_NC     /* EXIO1 */
#define BOARD_PIN_TOUCH_INT     GPIO_NUM_NC     /* GPIO4, unused: see above */

/* --- audio -----------------------------------------------------------------
 * ES8311 codec to the speaker and ES7210 with two microphones, one I2S bus,
 * both configured over I2C. */
#define BOARD_PIN_I2S_MCLK      GPIO_NUM_2
#define BOARD_PIN_I2S_BCLK      GPIO_NUM_48
#define BOARD_PIN_I2S_WS        GPIO_NUM_38
#define BOARD_PIN_I2S_DOUT      GPIO_NUM_47     /* to the ES8311 */
#define BOARD_PIN_I2S_DIN       GPIO_NUM_39     /* from the ES7210 */

/* The speaker amplifier's enable, active high. Waveshare's codec config says
 * it has no PA pin, and its audio driver then sets this one by hand
 * (Audio_PA_EN) -- without it the ES8311 plays perfectly into silence. */
#define BOARD_PIN_SPK_PA        GPIO_NUM_15

#endif /* BOARD_PINS_LCD185C_H */
