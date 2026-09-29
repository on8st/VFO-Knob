/* Board bring-up for the Waveshare ESP32-S3-Touch-LCD-1.85C (VFO_BOARD=lcd185c).
 *
 * The knob's board_init.c, less the audio mux, plus the TCA9554 expander:
 * on this board the panel's and the touch controller's resets are expander
 * pins, not GPIOs, so both have to be pulsed here -- before panel_init() and
 * hal_touch_init(), which are told they have no reset pin of their own.
 */
#include "board.h"
#include "board_pins.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exio;
static uint8_t                 s_exio_out = 0xFF;   /* the chip's power-on state */

#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03
#define EXIO_TIMEOUT_MS    20

/* Writes the whole output register from a shadow copy rather than
 * read-modify-write: one transaction, and no read to go wrong on a bus that
 * touch -- the PTT input -- shares. */
static esp_err_t exio_set(int pin, bool level)
{
    const uint8_t bit = (uint8_t)(1u << (pin - 1));
    const uint8_t v   = level ? (s_exio_out | bit) : (s_exio_out & ~bit);
    const uint8_t buf[2] = { TCA9554_OUTPUT_REG, v };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_exio, buf, sizeof buf,
                                            EXIO_TIMEOUT_MS), TAG, "exio write");
    s_exio_out = v;
    return ESP_OK;
}

static esp_err_t expander_init(void)
{
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BOARD_I2C_ADDR_EXPANDER,
        .scl_speed_hz    = BOARD_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev, &s_exio),
                        TAG, "expander");

    /* Outputs first, driven to the level they already float to (high: both
     * resets released), then every pin an output -- as Waveshare's EXIO_Init
     * does, with nothing on this board wired as an expander input. */
    const uint8_t out[2] = { TCA9554_OUTPUT_REG, s_exio_out };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_exio, out, sizeof out,
                                            EXIO_TIMEOUT_MS), TAG, "exio out");
    const uint8_t cfg[2] = { TCA9554_CONFIG_REG, 0x00 };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_exio, cfg, sizeof cfg,
                                            EXIO_TIMEOUT_MS), TAG, "exio cfg");

    /* Pulse both resets together: 10 ms low, then the vendor's 50 ms for the
     * controllers to come back before anyone talks to them. */
    ESP_RETURN_ON_ERROR(exio_set(BOARD_EXIO_TOUCH_RST, false), TAG, "tp rst");
    ESP_RETURN_ON_ERROR(exio_set(BOARD_EXIO_LCD_RST,   false), TAG, "lcd rst");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(exio_set(BOARD_EXIO_TOUCH_RST, true),  TAG, "tp rst");
    ESP_RETURN_ON_ERROR(exio_set(BOARD_EXIO_LCD_RST,   true),  TAG, "lcd rst");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "TCA9554 up at 0x%02X; panel and touch reset",
             BOARD_I2C_ADDR_EXPANDER);
    return ESP_OK;
}

esp_err_t board_init(void)
{
    /* Free a hung bus before claiming it -- the same reasoning as the knob's
     * board_init.c: a reset mid-read can leave a slave holding SDA low, and
     * a busy bus looks exactly like dead hardware. */
    gpio_config_t rec = {
        .pin_bit_mask = (1ULL << BOARD_PIN_I2C_SCL) | (1ULL << BOARD_PIN_I2C_SDA),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rec));
    if (gpio_get_level(BOARD_PIN_I2C_SDA) == 0) {
        ESP_LOGW(TAG, "SDA stuck low -- clocking the bus free");
        for (int i = 0; i < 9; i++) {
            gpio_set_level(BOARD_PIN_I2C_SCL, 0); esp_rom_delay_us(5);
            gpio_set_level(BOARD_PIN_I2C_SCL, 1); esp_rom_delay_us(5);
        }
        gpio_set_level(BOARD_PIN_I2C_SDA, 0); esp_rom_delay_us(5);
        gpio_set_level(BOARD_PIN_I2C_SCL, 1); esp_rom_delay_us(5);
        gpio_set_level(BOARD_PIN_I2C_SDA, 1); esp_rom_delay_us(5);
        ESP_LOGW(TAG, "recovery done, SDA=%d", gpio_get_level(BOARD_PIN_I2C_SDA));
    }

    i2c_master_bus_config_t bus = {
        .i2c_port                     = BOARD_I2C_PORT,
        .sda_io_num                   = BOARD_PIN_I2C_SDA,
        .scl_io_num                   = BOARD_PIN_I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        /* External pull-ups, as on the knob; Waveshare's BSP leaves the
         * internal ones off too. */
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    ESP_LOGI(TAG, "i2c up: sda=%d scl=%d @%d Hz",
             BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, BOARD_I2C_HZ);

    /* Without the expander neither the panel nor touch comes out of reset,
     * so this failing is the board failing. */
    return expander_init();
}

i2c_master_bus_handle_t board_i2c(void) { return s_i2c; }
