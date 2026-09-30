/* The audio hardware beneath components/audio: open the output, open the
 * microphone, read it. One implementation per board, each providing this same
 * header, and exactly one is built -- see VFO_BOARD in the top-level
 * CMakeLists.txt. Everything above it (buffering, volume, the microphone's
 * filter and limiter, capture only while keyed) is shared.
 *
 * This one: the knob. A PCM5100A DAC on I2S1 and a PDM microphone on I2S0.
 */
#ifndef AUDIO_HW_H
#define AUDIO_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_types.h"
#include "esp_err.h"

/* The PDM element is quiet: speech close to the knob peaked at 1-5% of full
 * scale, and the S3 has no PDM gain stage of its own. This fixed boost sets
 * where the operator's 0-200% sits. At 8x, 200% only reached -20 dB on
 * AetherSDR's mic Level gauge; at 32x it barely reached 0 with AetherSDR's TCI
 * TX slider at 100% (at its 50% default the radio sees the knob's full scale
 * as -6 dBFS). So 64x: 100% is where 200% was, and 200% is the most there is
 * -- peaks at the limiter, which is what the limiter's knee is for. */
#define AUDIO_HW_MIC_PREGAIN 64.0f

/* The volume law: the operator's 0..100 raised to this. Linear, as it has
 * always been, into a headphone jack. */
#define AUDIO_HW_VOL_EXP 1

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
