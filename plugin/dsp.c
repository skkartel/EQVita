#include "dsp.h"

#include <math.h>
#include <string.h>

#define EQ_Q_VALUE 0.707f
#define EQ_PI 3.14159265358979323846f

static int g_errno_stub;
int *__errno(void) { return &g_errno_stub; }

static inline float mdB_to_gain(int32_t mdB) {
    // mdB is in millibels (1000 mdB = 1 dB)
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static uint32_t normalize_sample_rate(uint32_t sample_rate) {
    if (sample_rate < 8000 || sample_rate > 192000) {
        return 48000;
    }
    return sample_rate;
}

static uint32_t clamp_filter_frequency(uint32_t sample_rate, uint32_t freq) {
    // Clamp frequency to Nyquist - margin to avoid instability
    uint32_t nyquist = sample_rate / 2;
    if (nyquist <= 100) {
        return nyquist > 1 ? nyquist - 1 : 1;
    }
    if (freq >= nyquist) {
        return nyquist - 100;
    }
    return freq;
}

static void biquad_compute(eq_biquad_t *out, uint32_t sample_rate, uint32_t freq, float gain) {
    sample_rate = normalize_sample_rate(sample_rate);
    freq = clamp_filter_frequency(sample_rate, freq);

    float omega = 2.0f * EQ_PI * ((float)freq) / (float)sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * EQ_Q_VALUE);
    
    // RBJ Peaking EQ: A = sqrt( 10^(dB/20) )
    float A = sqrtf(gain);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / A;

    float inv_a0 = 1.0f / a0;
    out->b0 = b0 * inv_a0;
    out->b1 = b1 * inv_a0;
    out->b2 = b2 * inv_a0;
    out->a1 = a1 * inv_a0;
    out->a2 = a2 * inv_a0;
}

static void biquad_identity(eq_biquad_t *out) {
    out->b0 = 1.0f;
    out->b1 = 0.0f;
    out->b2 = 0.0f;
    out->a1 = 0.0f;
    out->a2 = 0.0f;
}

static int biquad_same(const eq_biquad_t *a, const eq_biquad_t *b) {
    const float eps = 0.000001f;
    return fabsf(a->b0 - b->b0) < eps &&
           fabsf(a->b1 - b->b1) < eps &&
           fabsf(a->b2 - b->b2) < eps &&
           fabsf(a->a1 - b->a1) < eps &&
           fabsf(a->a2 - b->a2) < eps;
}

static void biquad_highpass(eq_biquad_t *out, uint32_t sample_rate, uint32_t freq) {
    sample_rate = normalize_sample_rate(sample_rate);
    freq = clamp_filter_frequency(sample_rate, freq);

    float omega = 2.0f * EQ_PI * ((float)freq) / (float)sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * 0.707f); // Q = 0.707 (Butterworth)

    float b0 = (1.0f + cs) / 2.0f;
    float b1 = -(1.0f + cs);
    float b2 = (1.0f + cs) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha;

    float inv_a0 = 1.0f / a0;
    out->b0 = b0 * inv_a0;
    out->b1 = b1 * inv_a0;
    out->b2 = b2 * inv_a0;
    out->a1 = a1 * inv_a0;
    out->a2 = a2 * inv_a0;
}

static void reset_delay_state(eq_dsp_state_t *state) {
    memset(state->band_z, 0, sizeof(state->band_z));
    memset(state->hpf_z, 0, sizeof(state->hpf_z));
}

static void reset_band_delay_state(eq_dsp_state_t *state, int band) {
    if (!state || band < 0 || band >= EQ_BANDS) {
        return;
    }

    for (int ch = 0; ch < EQ_DSP_MAX_CHANNELS; ++ch) {
        state->band_z[ch][band].z1 = 0.0f;
        state->band_z[ch][band].z2 = 0.0f;
    }
}

static void flush_denormal_delay_state(eq_dsp_state_t *state, uint32_t channels) {
    if (!state) {
        return;
    }

    if (channels > EQ_DSP_MAX_CHANNELS) {
        channels = EQ_DSP_MAX_CHANNELS;
    }

    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (fabsf(state->hpf_z[ch].z1) < 1e-15f) state->hpf_z[ch].z1 = 0.0f;
        if (fabsf(state->hpf_z[ch].z2) < 1e-15f) state->hpf_z[ch].z2 = 0.0f;
        for (int b = 0; b < EQ_BANDS; ++b) {
            if (fabsf(state->band_z[ch][b].z1) < 1e-15f) state->band_z[ch][b].z1 = 0.0f;
            if (fabsf(state->band_z[ch][b].z2) < 1e-15f) state->band_z[ch][b].z2 = 0.0f;
        }
    }
}

static inline int16_t limit_i16(float x, int32_t *clip_counter) {
    const float knee = 32600.0f;
    float sign = 1.0f;
    float ax = x;
    float limit = 32767.0f;
    float knee_range;

    if (x < 0.0f) {
        sign = -1.0f;
        ax = -x;
        limit = 32768.0f;
    }

    if (ax > limit) {
        float over = ax - knee;
        if (clip_counter) {
            (*clip_counter)++;
        }
        knee_range = limit - knee;
        ax = knee + (knee_range * over) / (over + knee_range);
        x = sign * ax;
    }

    x = (x >= 0.0f) ? (x + 0.5f) : (x - 0.5f);
    if (x > 32767.0f) {
        return 32767;
    }
    if (x < -32768.0f) {
        return -32768;
    }
    return (int16_t)x;
}

static inline uint16_t abs_i16_peak(int16_t value) {
    if (value == (int16_t)-32768) {
        return 32768u;
    }
    return (uint16_t)(value < 0 ? -value : value);
}

void eq_dsp_init(eq_dsp_state_t *state, uint32_t sample_rate) {
    memset(state, 0, sizeof(*state));
    state->sample_rate = normalize_sample_rate(sample_rate);
    state->preamp = mdB_to_gain(EQ_DEFAULT_PREAMP_MDB);
    state->target_preamp = state->preamp;
    state->hpf_enabled = 0;
    
    for (int i = 0; i < EQ_BANDS; ++i) {
        biquad_identity(&state->active[i]);
        state->target[i] = state->active[i];
        state->active_band_enabled[i] = 0;
        state->target_band_enabled[i] = 0;
    }
    
    // Init HPF at 70Hz
    biquad_highpass(&state->hpf, state->sample_rate, 70);
    reset_delay_state(state);
}

void eq_dsp_set_targets(eq_dsp_state_t *state, uint32_t sample_rate, const int32_t *band_mdB, int32_t preamp_mdB, int hpf_enabled) {
    eq_biquad_t next_target[EQ_BANDS];
    uint8_t next_enabled[EQ_BANDS];
    float next_preamp;
    int changed = 0;

    if (!state) {
        return;
    }

    sample_rate = normalize_sample_rate(sample_rate);
    if (sample_rate != state->sample_rate) {
        state->sample_rate = sample_rate;
        reset_delay_state(state);
        biquad_highpass(&state->hpf, state->sample_rate, 70);
        changed = 1;
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        int32_t gain_mdB = band_mdB ? band_mdB[i] : 0;
        next_enabled[i] = (gain_mdB != 0) ? 1u : 0u;
        if (next_enabled[i]) {
            float gain = mdB_to_gain(gain_mdB);
            biquad_compute(&next_target[i], state->sample_rate, eq_band_frequencies[i], gain);
        } else {
            biquad_identity(&next_target[i]);
        }

        if (state->target_band_enabled[i] != next_enabled[i] ||
            !biquad_same(&state->target[i], &next_target[i])) {
            changed = 1;
        }
    }

    next_preamp = mdB_to_gain(preamp_mdB);
    if (fabsf(state->target_preamp - next_preamp) > 0.000001f ||
        state->hpf_enabled != (hpf_enabled ? 1 : 0)) {
        changed = 1;
    }

    if (!changed) {
        return;
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        state->target[i] = next_target[i];
        state->target_band_enabled[i] = next_enabled[i];
    }

    state->target_preamp = next_preamp;
    state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    state->hpf_enabled = hpf_enabled ? 1 : 0;
}

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline void lerp_biquad(const eq_biquad_t *a, const eq_biquad_t *b, float t, eq_biquad_t *out) {
    out->b0 = lerp(a->b0, b->b0, t);
    out->b1 = lerp(a->b1, b->b1, t);
    out->b2 = lerp(a->b2, b->b2, t);
    out->a1 = lerp(a->a1, b->a1, t);
    out->a2 = lerp(a->a2, b->a2, t);
}

static inline float process_biquad(const eq_biquad_t *c, eq_biquad_delay_t *z, float x) {
    float y = c->b0 * x + z->z1;
    z->z1 = c->b1 * x - c->a1 * y + z->z2;
    z->z2 = c->b2 * x - c->a2 * y;
    return y;
}

void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    if (!state || !pcm || channels < 1 || channels > EQ_DSP_MAX_CHANNELS) { return; }

    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        float t = 1.0f;
        if (state->smooth_remaining > 0) {
            t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
        }

        float preamp = (state->smooth_remaining > 0)
            ? lerp(state->preamp, state->target_preamp, t)
            : state->preamp;

        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (i * channels) + ch;
            float sample = (float)pcm[idx];
            
            // 1. Apply HPF if enabled
            if (state->hpf_enabled) {
                sample = process_biquad(&state->hpf, &state->hpf_z[ch], sample);
            }
            
            // 2. Apply Preamp
            sample *= preamp;

            // 3. Apply EQ Bands
            for (int b = 0; b < EQ_BANDS; ++b) {
                if (state->smooth_remaining > 0) {
                    if (!state->active_band_enabled[b] && !state->target_band_enabled[b]) {
                        continue;
                    }
                    eq_biquad_t tmp;
                    lerp_biquad(&state->active[b], &state->target[b], t, &tmp);
                    sample = process_biquad(&tmp, &state->band_z[ch][b], sample);
                } else if (state->active_band_enabled[b]) {
                    sample = process_biquad(&state->active[b], &state->band_z[ch][b], sample);
                }
            }

            // 4. Soft-knee limiter followed by integer clamping.
            int16_t out_val = limit_i16(sample, clip_counter);
            pcm[idx] = out_val;
            
            // Peak metering
            uint16_t abs_val = abs_i16_peak(out_val);
            if (ch == 0) {
                if (abs_val > max_l) max_l = abs_val;
            } else if (ch == 1) {
                if (abs_val > max_r) max_r = abs_val;
            }
        }

        if (state->smooth_remaining > 0) {
            state->smooth_remaining--;
            if (state->smooth_remaining == 0) {
                for (int b = 0; b < EQ_BANDS; ++b) {
                    state->active[b] = state->target[b];
                    state->active_band_enabled[b] = state->target_band_enabled[b];
                    if (!state->active_band_enabled[b]) {
                        reset_band_delay_state(state, b);
                    }
                }
                state->preamp = state->target_preamp;
            }
        }
    }

    flush_denormal_delay_state(state, channels);
    
    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

uint32_t eq_dsp_active_band_count(const eq_dsp_state_t *state) {
    uint32_t count = 0;

    if (!state) {
        return 0;
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        if (state->active_band_enabled[i]) {
            count++;
        }
    }

    return count;
}
