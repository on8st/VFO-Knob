#include "panel.h"
#include "board_pins.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "panel";

extern const st77916_lcd_init_cmd_t vfo_st77916_init_cmds_new[];
extern const size_t                 vfo_st77916_init_cmds_new_len;

#define LCD_BPP        16
#define BL_TIMER       LEDC_TIMER_3
#define BL_CHANNEL     LEDC_CHANNEL_1
#define BL_MODE        LEDC_LOW_SPEED_MODE

/* QSPI read opcode, in the top byte of the 32-bit command word. */
#define LCD_OPCODE_READ_CMD 0x0BU
/* The ID read runs slowly: at full clock the reply is not dependable. */
#define ID_READ_PCLK_HZ     (3 * 1000 * 1000)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

static void backlight_init(void)
{
    /* 8-bit like the knob's, so panel_set_brightness() means the same on both
     * boards. 20 kHz rather than the vendor's 5 kHz: this board has a
     * speaker, and 5 kHz of backlight PWM is well inside what it can sing. */
    ledc_timer_config_t t = {
        .speed_mode      = BL_MODE,
        .timer_num       = BL_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 20 * 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {
        .gpio_num   = BOARD_PIN_LCD_BL,
        .speed_mode = BL_MODE,
        .channel    = BL_CHANNEL,
        .timer_sel  = BL_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void panel_set_brightness(uint8_t duty)
{
    ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

/* Which panel revision this is. Waveshare ships two, and the init sequence
 * that suits one leaves the other blank: register 0x04 tells them apart. */
static const st77916_lcd_init_cmd_t *pick_init(uint16_t *count)
{
    esp_lcd_panel_io_spi_config_t cfg =
        ST77916_PANEL_IO_QSPI_CONFIG(BOARD_PIN_LCD_CS, NULL, NULL);
    cfg.pclk_hz = ID_READ_PCLK_HZ;
    esp_lcd_panel_io_handle_t io = NULL;
    uint8_t id[4] = { 0 };
    esp_err_t err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &cfg, &io);
    if (err == ESP_OK) {
        const int cmd = (int)((LCD_OPCODE_READ_CMD << 24) | (0x04U << 8));
        err = esp_lcd_panel_io_rx_param(io, cmd, id, sizeof id);
        /* Only for the read: kept, it would hold a second transaction pool
         * and its DMA memory for the life of the firmware. */
        esp_lcd_panel_io_del(io);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ID read failed (%s) -- using the default sequence",
                 esp_err_to_name(err));
        *count = 0;
        return NULL;
    }
    ESP_LOGI(TAG, "ST77916 ID %02X %02X %02X %02X", id[0], id[1], id[2], id[3]);
    if (id[0] == 0x00 && id[1] == 0x02 && id[2] == 0x7F && id[3] == 0x7F) {
        *count = (uint16_t)vfo_st77916_init_cmds_new_len;
        return vfo_st77916_init_cmds_new;
    }
    *count = 0;                           /* 00 7F 7F 7F: the driver's own */
    return NULL;
}

/* The controller comes up showing whatever its RAM held. */
static void clear(void)
{
    enum { LINES = 16 };
    uint16_t *buf = heap_caps_calloc(BOARD_LCD_H_RES * LINES, sizeof(uint16_t),
                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) return;
    for (int y = 0; y < BOARD_LCD_V_RES; y += LINES) {
        int y_end = y + LINES > BOARD_LCD_V_RES ? BOARD_LCD_V_RES : y + LINES;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, BOARD_LCD_H_RES, y_end, buf);
    }
    heap_caps_free(buf);
}

esp_err_t panel_init(void)
{
    backlight_init();

    const spi_bus_config_t bus = {
        .sclk_io_num     = BOARD_PIN_LCD_PCLK,
        .data0_io_num    = BOARD_PIN_LCD_DATA0,
        .data1_io_num    = BOARD_PIN_LCD_DATA1,
        .data2_io_num    = BOARD_PIN_LCD_DATA2,
        .data3_io_num    = BOARD_PIN_LCD_DATA3,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
        .isr_cpu_id      = PANEL_CORE == 0 ? ESP_INTR_CPU_AFFINITY_0
                                           : ESP_INTR_CPU_AFFINITY_1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus,
                                           SPI_DMA_CH_AUTO), TAG, "spi bus");

    uint16_t n_cmds = 0;
    const st77916_lcd_init_cmd_t *cmds = pick_init(&n_cmds);

    esp_lcd_panel_io_spi_config_t io_cfg =
        ST77916_PANEL_IO_QSPI_CONFIG(BOARD_PIN_LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = BOARD_LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &s_io),
        TAG, "panel io");

    st77916_vendor_config_t vendor = {
        .init_cmds      = cmds,
        .init_cmds_size = n_cmds,
        .flags = { .use_qspi_interface = 1 },
    };
    /* No reset pin: the panel's reset is an expander output, already pulsed
     * by board_init(), and the driver falls back to a software reset. */
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config  = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_io, &dev, &s_panel),
                        TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
    clear();

    /* Backlight on only now, so the power-on garbage is never lit. */
    panel_set_brightness(200);

    ESP_LOGI(TAG, "ST77916 up: %dx%d RGB565, QSPI on host %d, %s init",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_SPI_HOST,
             cmds ? "rev-2" : "default");
    return ESP_OK;
}

esp_lcd_panel_handle_t panel_handle(void) { return s_panel; }
esp_lcd_panel_io_handle_t panel_io_handle(void) { return s_io; }

/* --- M4 self-test: as the knob's, see components/panel/panel.c ----------- */

#define ROW_BYTES (BOARD_LCD_H_RES * (int)sizeof(uint16_t))

static esp_err_t fill(uint16_t *row, uint16_t colour)
{
    for (int x = 0; x < BOARD_LCD_H_RES; x++) row[x] = colour;
    for (int y = 0; y < BOARD_LCD_V_RES; y++)
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(
            s_panel, 0, y, BOARD_LCD_H_RES, y + 1, row), TAG, "blit");
    return ESP_OK;
}

esp_err_t panel_selftest(void)
{
    uint16_t *row = heap_caps_malloc(ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(row, ESP_ERR_NO_MEM, TAG, "row buffer");

    panel_set_brightness(255);

    const struct { uint16_t c; const char *name; } STEPS[] = {
        { 0xF800, "RED" }, { 0x07E0, "GREEN" }, { 0x001F, "BLUE" },
        { 0xFFFF, "WHITE" }, { 0x0000, "BLACK" },
    };
    for (size_t i = 0; i < sizeof STEPS / sizeof STEPS[0]; i++) {
        ESP_LOGI(TAG, "  [M4] full screen %s", STEPS[i].name);
        ESP_RETURN_ON_ERROR(fill(row, STEPS[i].c), TAG, "fill");
        vTaskDelay(pdMS_TO_TICKS(1400));
    }

    ESP_LOGI(TAG, "  [M4] 1 px ring at r=179 + centre crosshair");
    const int cx = BOARD_LCD_H_RES / 2, cy = BOARD_LCD_V_RES / 2, r = 179;
    for (int y = 0; y < BOARD_LCD_V_RES; y++) {
        for (int x = 0; x < BOARD_LCD_H_RES; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            bool ring  = d2 <= r * r && d2 > (r - 1) * (r - 1);
            bool cross = (x == cx || y == cy) && d2 < 400;
            row[x] = ring ? 0xFFFF : cross ? 0xF81F : 0x0000;
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(
            s_panel, 0, y, BOARD_LCD_H_RES, y + 1, row), TAG, "blit");
    }
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "  [M4] backlight ramp (watch the screen dim and come back)");
    ESP_RETURN_ON_ERROR(fill(row, 0xFFFF), TAG, "fill");
    for (int rep = 0; rep < 2; rep++) {
        for (int d = 255; d >= 0; d -= 4) { panel_set_brightness((uint8_t)d); vTaskDelay(pdMS_TO_TICKS(10)); }
        for (int d = 0; d <= 255; d += 4) { panel_set_brightness((uint8_t)d); vTaskDelay(pdMS_TO_TICKS(10)); }
    }
    panel_set_brightness(255);

    heap_caps_free(row);
    ESP_LOGI(TAG, "  [M4] self-test done");
    return ESP_OK;
}
