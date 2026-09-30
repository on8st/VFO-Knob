/* The lcd185c's audio hardware: an ES8311 codec driving the speaker and an
 * ES7210 ADC reading two microphones, both on one full-duplex I2S bus (they
 * share its clocks) and both set up over the shared I2C bus.
 *
 * esp_codec_dev is used for the codecs' registers only. The I2S channels stay
 * ours, opened and enabled the way the knob's are, so the shared code above
 * -- which enables the microphone only while keyed -- keeps control of them.
 */
#include "audio_hw.h"
#include "board.h"
#include "board_pins.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx, s_rx;
static uint32_t          s_rate;
static bool              s_rx_on;

/* Both directions at once, because both are one bus: whichever of the output
 * or the microphone is opened first opens it for the other. */
static esp_err_t bus_open(uint32_t rate)
{
    if (s_tx) return rate == s_rate ? ESP_OK : ESP_ERR_INVALID_ARG;

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* The knob's budget: internal RAM is as contended here as there. */
    cc.dma_desc_num  = 4;
    cc.dma_frame_num = 240;
    /* Zero played buffers, or a stalled stream loops its last 30 ms. */
    cc.auto_clear    = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "chan");

    /* One clock for both: MCLK at 256 x fs, which the ES7210 needs and the
     * ES8311 is told it has (use_mclk below). */
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_PIN_I2S_MCLK,
            .bclk = BOARD_PIN_I2S_BCLK,
            .ws   = BOARD_PIN_I2S_WS,
            .dout = BOARD_PIN_I2S_DOUT,
            .din  = BOARD_PIN_I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &sc), TAG, "tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &sc), TAG, "rx std");
    /* Transmit runs from here on, whether or not the speaker's codec comes
     * up: in full duplex the receive side is clocked by it, so without it
     * the microphone would never deliver a sample. Idle, it sends the
     * silence auto_clear leaves behind. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable");
    s_rate = rate;
    return ESP_OK;
}

/* A codec's control interface on the board's I2C bus. esp_codec_dev takes
 * the address in its 8-bit form. */
static const audio_codec_ctrl_if_t *ctrl(uint8_t addr7)
{
    audio_codec_i2c_cfg_t c = {
        .port       = BOARD_I2C_PORT,
        .addr       = (uint8_t)(addr7 << 1),
        .bus_handle = board_i2c(),
    };
    return audio_codec_new_i2c_ctrl(&c);
}

static esp_err_t codec_start(const audio_codec_if_t *codec, uint32_t rate)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = rate,
    };
    if (codec->set_fs(codec, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    if (codec->enable(codec, true) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t audio_hw_out_open(uint32_t rate, i2s_chan_handle_t *tx)
{
    ESP_RETURN_ON_ERROR(bus_open(rate), TAG, "i2s");

    const audio_codec_ctrl_if_t *ci = ctrl(BOARD_I2C_ADDR_DAC);
    ESP_RETURN_ON_FALSE(ci, ESP_FAIL, TAG, "es8311 ctrl");
    es8311_codec_cfg_t cfg = {
        .ctrl_if     = ci,
        .gpio_if     = audio_codec_new_gpio(),
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = BOARD_PIN_SPK_PA,   /* switched on with the codec */
        .master_mode = false,      /* the S3 drives the clocks */
        .use_mclk    = true,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "es8311");
    ESP_RETURN_ON_ERROR(codec_start(codec, rate), TAG, "es8311 start");
    /* Full scale in the codec; the operator's volume is applied in software
     * above, as on the knob, so it means the same on both. */
    codec->set_vol(codec, 0.0f);

    *tx = s_tx;                        /* enabled already, by bus_open() */
    return ESP_OK;
}

esp_err_t audio_hw_in_open(uint32_t rate)
{
    ESP_RETURN_ON_ERROR(bus_open(rate), TAG, "i2s");

    const audio_codec_ctrl_if_t *ci = ctrl(BOARD_I2C_ADDR_ADC);
    ESP_RETURN_ON_FALSE(ci, ESP_FAIL, TAG, "es7210 ctrl");
    es7210_codec_cfg_t cfg = {
        .ctrl_if      = ci,
        .master_mode  = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *codec = es7210_codec_new(&cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "es7210");
    ESP_RETURN_ON_ERROR(codec_start(codec, rate), TAG, "es7210 start");
    codec->set_mic_gain(codec, AUDIO_HW_MIC_GAIN_DB);
    /* The channel itself stays disabled until PTT asks for it. */
    return ESP_OK;
}

esp_err_t audio_hw_in_enable(bool on)
{
    if (on == s_rx_on) return ESP_OK;
    s_rx_on = on;
    return on ? i2s_channel_enable(s_rx) : i2s_channel_disable(s_rx);
}

/* The bus carries the two microphones as left and right. The caller wants
 * one, so they are averaged: the same voice at each, twice the signal, noise
 * that does not add up the same way. */
esp_err_t audio_hw_in_read(int16_t *out, size_t max, size_t *got,
                           uint32_t timeout_ms)
{
    static int16_t st[2 * 256];
    if (max > sizeof st / sizeof st[0] / 2) max = sizeof st / sizeof st[0] / 2;
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_rx, st, max * 2 * sizeof(int16_t),
                                     &bytes, pdMS_TO_TICKS(timeout_ms));
    const size_t frames = bytes / (2 * sizeof(int16_t));
    for (size_t i = 0; i < frames; i++)
        out[i] = (int16_t)(((int32_t)st[2 * i] + st[2 * i + 1]) / 2);
    *got = frames;
    return err;
}

const char *audio_hw_describe(void)
{
    return "ES8311 to the speaker, ES7210 from two mics, one I2S bus";
}
