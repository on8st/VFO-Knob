/* Audio for a board whose codec path is not written yet (lcd185c: ES8311 to
 * the speaker, ES7210 from two microphones). Same interface as audio_out.c
 * and audio_in.c, doing nothing -- and saying so, because silence with no
 * explanation looks exactly like a radio that is not sending audio.
 *
 * Nothing here can transmit sound: audio_in_take() never has any, so the
 * transmit path sends the silence it sends whenever the microphone is
 * starved.
 */
#include "audio_in.h"
#include "audio_out.h"

#include "esp_log.h"

static const char *TAG = "audio";

esp_err_t audio_out_init(void)
{
    ESP_LOGW(TAG, "no audio output on this board yet");
    return ESP_ERR_NOT_SUPPORTED;
}

bool audio_out_feed(const void *frame, size_t len)
{
    (void)frame; (void)len;
    return false;
}

bool audio_out_feed_pcm16(const int16_t *pcm, size_t frames, uint8_t channels)
{
    (void)pcm; (void)frames; (void)channels;
    return false;
}

void audio_out_set_volume(uint8_t vol) { (void)vol; }

void audio_out_stats(audio_stats_t *st)
{
    if (st) *st = (audio_stats_t){ 0 };
}

esp_err_t audio_in_init(void)
{
    ESP_LOGW(TAG, "no microphone on this board yet");
    return ESP_ERR_NOT_SUPPORTED;
}

void audio_in_set_active(bool on) { (void)on; }
bool audio_in_active(void)        { return false; }

bool audio_in_take(int16_t *out, size_t samples)
{
    (void)out; (void)samples;
    return false;
}

void audio_in_set_gain(uint8_t percent) { (void)percent; }

void audio_in_stats(audio_in_stats_t *st)
{
    if (st) *st = (audio_in_stats_t){ 0 };
}
