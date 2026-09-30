#include "audio_out.h"
#include "audio_hw.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static const char *TAG = "audio";

/* ~0.5 s at 24 kHz stereo int16. Lives in PSRAM: it is streamed through, not
 * touched by DMA, so it has no business competing for internal RAM -- which is
 * the scarce resource that the display and WiFi are already fighting over. */
#define RING_BYTES (48 * 1024)
/* Wait for this much before starting playback, so a burst of jitter at the
 * start of a stream does not produce an immediate underrun. */
#define PREROLL_BYTES (8 * 1024)

static i2s_chan_handle_t s_tx;
static RingbufHandle_t   s_ring;
static volatile uint8_t  s_vol = 40;
static volatile bool     s_playing;
static audio_stats_t     s_stats;
static int16_t           s_conv[1024];   /* scratch, playback task only */

void audio_out_set_volume(uint8_t v) { s_vol = v > 100 ? 100 : v; }

void audio_out_stats(audio_stats_t *st) { if (st) *st = s_stats; }

static void play_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Pre-roll. Starting playback the instant the first bytes arrive means
         * the very next scheduling hiccup is an audible gap; waiting for a
         * cushion first costs a few tens of milliseconds once, at the start of
         * the stream, and nothing thereafter. The previous version had this
         * check with an empty body and underran about once a second. */
        size_t buffered = RING_BYTES - xRingbufferGetCurFreeSize(s_ring);
        if (!s_playing) {
            if (buffered < PREROLL_BYTES) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
            s_playing = true;
        }

        size_t n = 0;
        uint8_t *p = xRingbufferReceiveUpTo(s_ring, &n, pdMS_TO_TICKS(60),
                                            sizeof s_conv);
        if (!p) {
            /* Ran dry: go back to buffering rather than stuttering along the
             * bottom of the ring. */
            if (s_playing) { s_stats.underruns++; s_playing = false; }
            continue;
        }

        /* Volume in the playback path, never on the network path: that one
         * must stay allocation-free and as short as possible, because it
         * shares a socket with PTT. */
        int16_t *smp   = (int16_t *)p;
        size_t   count = n / sizeof(int16_t);
#if AUDIO_HW_VOL_EXP > 1
        /* An audio taper, for output that is loud from the first notch: the
         * operator's 0..100 is raised to AUDIO_HW_VOL_EXP, so the fine steps
         * are at the quiet end. Recomputed only when the volume changes. */
        static uint8_t last_vol = 0xFF;
        static int32_t g_q15 = 32768;
        const uint8_t  vol = s_vol;
        if (vol != last_vol) {
            last_vol = vol;
            g_q15 = (int32_t)(powf(vol / 100.0f, AUDIO_HW_VOL_EXP) * 32768.0f);
        }
        if (vol != 100) {
            for (size_t i = 0; i < count; i++)
                smp[i] = (int16_t)(((int32_t)smp[i] * g_q15) >> 15);
        }
#else
        if (s_vol != 100) {
            int32_t g = s_vol;
            for (size_t i = 0; i < count; i++)
                smp[i] = (int16_t)(((int32_t)smp[i] * g) / 100);
        }
#endif

        size_t written = 0;
        i2s_channel_write(s_tx, smp, n, &written, portMAX_DELAY);
        vRingbufferReturnItem(s_ring, p);
    }
}

esp_err_t audio_out_init(void)
{
    s_ring = xRingbufferCreateWithCaps(RING_BYTES, RINGBUF_TYPE_BYTEBUF,
                                       MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_ring, ESP_ERR_NO_MEM, TAG, "ring");

    /* The board's own output, 16-bit stereo at AUDIO_RATE_HZ: see audio_hw.h
     * for what is underneath. */
    ESP_RETURN_ON_ERROR(audio_hw_out_open(AUDIO_RATE_HZ, &s_tx), TAG, "hw");

    s_stats.sample_rate = AUDIO_RATE_HZ;
    s_stats.channels    = AUDIO_CHANNELS;

    /* Core 1 with the rest of the "feel" work; priority above the UI so a
     * redraw cannot cause an audible gap, but below the knob. */
    xTaskCreatePinnedToCore(play_task, "audio", 3072, NULL, 11, NULL, 1);
    ESP_LOGI(TAG, "I2S up: %d Hz stereo, %s, %d kB PSRAM ring",
             AUDIO_RATE_HZ, audio_hw_describe(), RING_BYTES / 1024);
    return ESP_OK;
}

bool audio_out_feed_pcm16(const int16_t *pcm, size_t frames, uint8_t channels)
{
    if (!s_ring || !pcm || !frames || (channels != 1 && channels != 2)) return false;
    s_stats.format      = TCI_AUDIO_FMT_INT16;
    s_stats.sample_rate = AUDIO_RATE_HZ;
    s_stats.channels    = channels;

    static int16_t tmp[512];                 /* the one network task feeds it */
    size_t done = 0;
    while (done < frames) {
        size_t chunk = frames - done;
        if (chunk > sizeof tmp / sizeof tmp[0] / 2) chunk = sizeof tmp / sizeof tmp[0] / 2;
        if (channels == 1) {
            for (size_t i = 0; i < chunk; i++)
                tmp[2 * i] = tmp[2 * i + 1] = pcm[done + i];
        } else {
            memcpy(tmp, pcm + 2 * done, chunk * 4);
        }
        if (xRingbufferSend(s_ring, tmp, chunk * 4, 0) != pdTRUE) {
            s_stats.dropped++;
            return false;
        }
        done += chunk;
    }
    s_stats.frames++;
    return true;
}

bool audio_out_feed(const void *frame, size_t len)
{
    if (!s_ring || len <= sizeof(tci_audio_hdr_t)) return false;

    const tci_audio_hdr_t *h = frame;
    if (h->type != TCI_AUDIO_TYPE_RX) return false;

    const uint8_t *payload = (const uint8_t *)frame + sizeof *h;
    size_t         avail   = len - sizeof *h;
    uint32_t       count   = h->length;          /* REAL samples, not frames */

    s_stats.format      = h->format;
    s_stats.sample_rate = h->sample_rate;
    s_stats.channels    = h->channels;

    /* Convert into a bounded stack buffer, in chunks. int16 passes straight
     * through; float32 is scaled and saturated. */
    static int16_t tmp[512];
    size_t done = 0;

    while (done < count) {
        size_t chunk = count - done;
        if (chunk > sizeof tmp / sizeof tmp[0]) chunk = sizeof tmp / sizeof tmp[0];

        if (h->format == TCI_AUDIO_FMT_INT16) {
            if ((done + chunk) * 2 > avail) break;
            memcpy(tmp, payload + done * 2, chunk * 2);
        } else if (h->format == TCI_AUDIO_FMT_FLOAT32) {
            if ((done + chunk) * 4 > avail) break;
            const float *f = (const float *)(payload + done * 4);
            for (size_t i = 0; i < chunk; i++) {
                float v = f[i] * 32767.0f;
                if (v >  32767.0f) v =  32767.0f;
                if (v < -32768.0f) v = -32768.0f;
                tmp[i] = (int16_t)v;
            }
        } else {
            return false;                 /* int24/int32 not requested */
        }

        /* Never block the WebSocket task. Dropping audio is always better than
         * stalling the socket that also carries PTT. */
        if (xRingbufferSend(s_ring, tmp, chunk * 2, 0) != pdTRUE) {
            s_stats.dropped++;
            return false;
        }
        done += chunk;
    }
    s_stats.frames++;
    return true;
}
