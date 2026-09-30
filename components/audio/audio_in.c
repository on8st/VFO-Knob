#include "audio_in.h"
#include "audio_hw.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static const char *TAG = "mic";

/* ~340 ms of mono int16 at 24 kHz, in PSRAM. Generous, because the consumer is
 * paced by the server's chrono and a WiFi hiccup should cost latency, not a
 * hole in the transmitted audio. */
#define MIC_RING_BYTES (16 * 1024)
#define MIC_READ_SAMPLES 512
/* A fixed boost that sets where the operator's 0-200% sits. It belongs to the
 * microphone, so the board's audio_hw.h chooses it: 64x for the knob's quiet
 * PDM element (see there for how that was arrived at). */
#define MIC_PREGAIN AUDIO_HW_MIC_PREGAIN
/* Above this the level is rounded off rather than clipped: y = k + (1-k) *
 * u/(1+u), which leaves the slope continuous at the knee and never quite
 * reaches full scale. -3 dBFS. At 200% loud syllables get here; hard clipping
 * there would be harsh on the air. */
#define MIC_KNEE (0.7f * 32767.0f)
/* One-pole DC blocker, corner ~19 Hz at 24 kHz. Boosting the element's DC
 * offset along with the speech would spend headroom on nothing. */
#define MIC_DC_POLE 0.995f

static RingbufHandle_t    s_ring;
static volatile bool      s_active;
static volatile bool      s_priming;    /* start of an over: build a cushion */
static volatile bool      s_dc_reset;
static volatile uint8_t   s_gain = 100;
static audio_in_stats_t   s_stats;
static TaskHandle_t       s_task;

bool audio_in_active(void) { return s_active; }
void audio_in_set_gain(uint8_t g) { s_gain = g > 200 ? 200 : g; }
/* The peak is held from one call to the next, then starts again: the loudest
 * moment since the last look, not whichever block happened to be last --
 * which, in speech, is as often the gap between two syllables. */
void audio_in_stats(audio_in_stats_t *st)
{
    if (!st) return;
    *st = s_stats;
    s_stats.peak = 0.0f;
}

void audio_in_set_active(bool on)
{
    if (on == s_active) return;
    s_active = on;
    if (on) {
        /* Drop anything stale so the over starts with live audio, not with
         * whatever was in the buffer when the last one ended. */
        size_t n;
        void *p;
        while ((p = xRingbufferReceiveUpTo(s_ring, &n, 0, MIC_RING_BYTES)))
            vRingbufferReturnItem(s_ring, p);
        s_priming  = true;
        s_dc_reset = true;
        audio_hw_in_enable(true);
        ESP_LOGI(TAG, "capture ON");
    } else {
        audio_hw_in_enable(false);
        ESP_LOGI(TAG, "capture OFF");
    }
}

static void mic_task(void *arg)
{
    (void)arg;
    static int16_t buf[MIC_READ_SAMPLES];
    float x1 = 0.0f, y1 = 0.0f;              /* DC blocker state */

    for (;;) {
        if (!s_active) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

        size_t count = 0;
        if (audio_hw_in_read(buf, MIC_READ_SAMPLES, &count, 100) != ESP_OK ||
            !count)
            continue;
        const size_t got = count * sizeof(int16_t);
        if (s_dc_reset) {
            /* Start from the first sample, so the offset is not a step. */
            s_dc_reset = false;
            x1 = buf[0];
            y1 = 0.0f;
        }
        const float g = MIC_PREGAIN * (float)s_gain / 100.0f;
        for (size_t i = 0; i < count; i++) {
            const float x = buf[i];
            const float y = x - x1 + MIC_DC_POLE * y1;
            x1 = x;
            y1 = y;
            float v = y * g;
            const float a = fabsf(v);
            if (a > MIC_KNEE) {
                const float u   = (a - MIC_KNEE) / (32767.0f - MIC_KNEE);
                const float lim = MIC_KNEE + (32767.0f - MIC_KNEE) * u / (1.0f + u);
                v = v < 0.0f ? -lim : lim;
            }
            buf[i] = (int16_t)v;
        }
        /* Peak, for a mic-level indicator and for catching a dead microphone
         * before an operator discovers it mid-QSO. */
        int32_t pk = 0;
        for (size_t i = 0; i < count; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > pk) pk = a;
        }
        const float pkf = (float)pk / 32768.0f;
        if (pkf > s_stats.peak) s_stats.peak = pkf;

        if (xRingbufferSend(s_ring, buf, got, 0) != pdTRUE) s_stats.overruns++;
        else                                                s_stats.blocks++;
    }
}

esp_err_t audio_in_init(void)
{
    s_ring = xRingbufferCreateWithCaps(MIC_RING_BYTES, RINGBUF_TYPE_BYTEBUF,
                                       MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_ring, ESP_ERR_NO_MEM, TAG, "ring");

    /* The board's own microphone, 16-bit mono: see audio_hw.h. */
    ESP_RETURN_ON_ERROR(audio_hw_in_open(TX_AUDIO_RATE_HZ), TAG, "hw");

    /* Not enabled here: the microphone stays off until PTT asks for it. */
    xTaskCreatePinnedToCore(mic_task, "mic", 3072, NULL, 11, &s_task, 1);
    ESP_LOGI(TAG, "mic ready at %d Hz, %s (idle until PTT)",
             TX_AUDIO_RATE_HZ, audio_hw_describe());
    return ESP_OK;
}

bool audio_in_take(int16_t *out, size_t samples)
{
    if (!out || !samples) return false;
    const size_t want = samples * sizeof(int16_t);

    /* Hold back at the start of an over until two requests' worth is
     * buffered. The microphone delivers in DMA-sized steps and the requests
     * arrive over a network, so without a cushion the two clocks meet at the
     * wrong moment every few frames and each meeting is a hole in the audio.
     * Costs 43 ms of latency, once. */
    if (s_priming) {
        const size_t buffered = MIC_RING_BYTES - xRingbufferGetCurFreeSize(s_ring);
        if (buffered < 2 * want) return false;
        s_priming = false;
    }

    /* A byte ring hands back contiguous runs, so a read across the wrap comes
     * back short; the second read collects the rest. Each run is returned
     * before the next is taken, as a byte ring requires. */
    size_t have = 0;
    for (int pass = 0; pass < 2 && have < want; pass++) {
        size_t   got = 0;
        uint8_t *p   = xRingbufferReceiveUpTo(s_ring, &got, 0, want - have);
        if (!p) break;
        memcpy((uint8_t *)out + have, p, got);
        vRingbufferReturnItem(s_ring, p);
        have += got;
    }
    if (have < want) {
        memset((uint8_t *)out + have, 0, want - have);
        s_stats.starved++;
    }
    return have > 0;
}
