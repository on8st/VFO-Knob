/* The knob's audio hardware: what audio_out.c and audio_in.c used to set up
 * themselves, moved here unchanged so a board with different hardware can put
 * its own beneath the same code. */
#include "audio_hw.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

static i2s_chan_handle_t s_rx;

esp_err_t audio_hw_out_open(uint32_t rate, i2s_chan_handle_t *tx)
{
    /* GPIO0 HIGH hands the PCM5100A to the ESP32-S3 rather than the secondary
     * ESP32. board_init() already does this; assert it here too because a
     * silent mux in the wrong position produces perfect logs and no sound. */
    gpio_set_level(BOARD_PIN_AUDIO_MUX_SEL, 1);

    /* I2S1 explicitly, not AUTO: PDM receive for the microphone is only
     * available on I2S0 on the ESP32-S3, so the DAC must not take it. */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    /* 4 x 180 frames = 2.9 kB of DMA memory, about half the default. Internal
     * RAM is the contended resource on this board; at 24 kHz this is still
     * 30 ms of buffering, comfortably more than the scheduler needs. */
    cc.dma_desc_num  = 4;
    cc.dma_frame_num = 180;
    /* Zero each DMA buffer once it has been played. Without this the DMA
     * keeps cycling through whatever it last held whenever the stream stops
     * -- a network stall, or the radio going quiet -- and the knob loops the
     * last 30 ms of audio until data returns. */
    cc.auto_clear    = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, tx, NULL), TAG, "chan");

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,     /* PCM5100A derives its own clock */
            .bclk = BOARD_PIN_I2S_BCLK,
            .ws   = BOARD_PIN_I2S_WS,
            .dout = BOARD_PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*tx, &sc), TAG, "std");
    return i2s_channel_enable(*tx);
}

esp_err_t audio_hw_in_open(uint32_t rate)
{
    /* PDM receive exists only on I2S0 on the ESP32-S3; the DAC output is
     * pinned to I2S1 for exactly this reason. */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num  = 4;
    cc.dma_frame_num = 240;
    cc.auto_clear    = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, NULL, &s_rx), TAG, "chan");

    i2s_pdm_rx_config_t pc = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = BOARD_PIN_PDM_CLK,
            .din = BOARD_PIN_PDM_DATA,
            .invert_flags = { .clk_inv = false },
        },
    };
    return i2s_channel_init_pdm_rx_mode(s_rx, &pc);
}

esp_err_t audio_hw_in_enable(bool on)
{
    return on ? i2s_channel_enable(s_rx) : i2s_channel_disable(s_rx);
}

esp_err_t audio_hw_in_read(int16_t *out, size_t max, size_t *got,
                           uint32_t timeout_ms)
{
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_rx, out, max * sizeof(int16_t), &bytes,
                                     pdMS_TO_TICKS(timeout_ms));
    *got = bytes / sizeof(int16_t);
    return err;
}

const char *audio_hw_describe(void)
{
    return "PCM5100A on I2S1, PDM mic on I2S0";
}
