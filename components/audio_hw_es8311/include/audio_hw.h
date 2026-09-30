/* The audio hardware beneath components/audio: open the output, open the
 * microphone, read it. One implementation per board, each providing this same
 * header, and exactly one is built -- see VFO_BOARD in the top-level
 * CMakeLists.txt. Everything above it (buffering, volume, the microphone's
 * filter and limiter, capture only while keyed) is shared.
 *
 * This one: the lcd185c. An ES8311 codec to the speaker and an ES7210 ADC
 * from two microphones, sharing one full-duplex I2S bus.
 */
#ifndef AUDIO_HW_H
#define AUDIO_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_types.h"
#include "esp_err.h"

/* The ES7210 has an analogue gain stage of its own (AUDIO_HW_MIC_GAIN_DB,
 * Waveshare's figure for this board), so the knob's 64x would only clip
 * here. Calibrated on the BOX: normal speech at operating distance peaked at
 * 0.12-0.13 of full scale at 2x ([MIC] peak in the log), so 8x puts it near
 * 0.5 at the operator's 100%, with the limiter taking the loud syllables.
 * Checked on a real over into AetherSDR: peaks of 0.29-0.57 held over each
 * 2 s, and the level AetherSDR reports back lands in the amber (-10..0 dB). */
#define AUDIO_HW_MIC_PREGAIN 8.0f

/* The volume law: the operator's 0..100 raised to this. Cubic, an audio
 * taper: the speaker is loud from the first notch, and linear left all the
 * useful range in the bottom few percent (heard on the BOX: 0 to 2 was
 * already loud). 10% is -60 dB, 40% -24 dB, 70% -9 dB. */
#define AUDIO_HW_VOL_EXP 3
#define AUDIO_HW_MIC_GAIN_DB 30.0f

/* The output, 16-bit stereo at `rate`, enabled and ready to write. */
esp_err_t audio_hw_out_open(uint32_t rate, i2s_chan_handle_t *tx);

/* The microphone, 16-bit mono at `rate`. Opened idle: nothing is captured
 * until audio_hw_in_enable(true). */
esp_err_t audio_hw_in_open(uint32_t rate);
esp_err_t audio_hw_in_enable(bool on);

/* Up to `max` mono samples into `out`; `*got` says how many. */
esp_err_t audio_hw_in_read(int16_t *out, size_t max, size_t *got,
                           uint32_t timeout_ms);

/* One line for the boot log. */
const char *audio_hw_describe(void);

#endif /* AUDIO_HW_H */
