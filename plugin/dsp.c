#include "dsp.h"

#include <math.h>
#include <string.h>

#define EQ_Q_VALUE 0.707f
#define EQ_PI 3.14159265358979323846f

static int g_errno_stub;
int *__errno(void) { return &g_errno_stub; }

static void biquad_identity(eq_biquad_t *out);

#define EQ_RUNTIME_GAIN_MIN 0.25f
#define EQ_RUNTIME_GAIN_MAX 4.0f
#define EQ_RUNTIME_GAIN_DEFAULT 0.5011872f

static inline float sanitize_runtime_gain(float gain, float fallback) {
    if (isfinite(gain) && gain >= EQ_RUNTIME_GAIN_MIN && gain <= EQ_RUNTIME_GAIN_MAX) {
        return gain;
    }
    if (isfinite(fallback) && fallback >= EQ_RUNTIME_GAIN_MIN && fallback <= EQ_RUNTIME_GAIN_MAX) {
        return fallback;
    }
    return EQ_RUNTIME_GAIN_DEFAULT;
}

static inline void sanitize_preamp_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    state->target_preamp = sanitize_runtime_gain(state->target_preamp, EQ_RUNTIME_GAIN_DEFAULT);
    state->preamp = sanitize_runtime_gain(state->preamp, state->target_preamp);
}

static inline void sanitize_smoothing_state(eq_dsp_state_t *state) {
    if (state && state->smooth_remaining > EQ_SMOOTH_SAMPLES) {
        state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    }
}

static inline float mdB_to_gain(int32_t mdB) {
    // mdB is in millibels (1000 mdB = 1 dB)
    mdB = eq_clamp_mdB(mdB);
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

    if (!(gain > 0.0f) || !isfinite(gain)) {
        biquad_identity(out);
        return;
    }

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

static int biquad_is_finite(const eq_biquad_t *c) {
    return c &&
           isfinite(c->b0) &&
           isfinite(c->b1) &&
           isfinite(c->b2) &&
           isfinite(c->a1) &&
           isfinite(c->a2);
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

static void reset_hpf_delay_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

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

static void rebuild_band_index(const uint8_t *enabled, uint8_t *index, uint8_t *count) {
    uint8_t n = 0;

    if (!enabled || !index || !count) {
        return;
    }

    for (uint8_t b = 0; b < EQ_BANDS; ++b) {
        if (enabled[b]) {
            index[n++] = b;
        }
    }

    *count = n;
}

static void rebuild_dsp_band_indexes(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    rebuild_band_index(state->active_band_enabled, state->active_band_index, &state->active_band_count);
    rebuild_band_index(state->target_band_enabled, state->target_band_index, &state->target_band_count);
}

static void sanitize_biquad_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    if (!biquad_is_finite(&state->hpf)) {
        biquad_highpass(&state->hpf, state->sample_rate, 70);
        reset_hpf_delay_state(state);
    }

    for (int b = 0; b < EQ_BANDS; ++b) {
        if (!biquad_is_finite(&state->target[b])) {
            biquad_identity(&state->target[b]);
            state->target_band_enabled[b] = 0;
        }

        if (!biquad_is_finite(&state->active[b])) {
            if (state->target_band_enabled[b]) {
                state->active[b] = state->target[b];
                state->active_band_enabled[b] = 1;
            } else {
                biquad_identity(&state->active[b]);
                state->active_band_enabled[b] = 0;
            }
            reset_band_delay_state(state, b);
        }
    }

    rebuild_dsp_band_indexes(state);
}

static void flush_denormal_delay_state(eq_dsp_state_t *state, uint32_t channels) {
    if (!state) {
        return;
    }

    if (channels > EQ_DSP_MAX_CHANNELS) {
        channels = EQ_DSP_MAX_CHANNELS;
    }

    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!isfinite(state->hpf_z[ch].z1) || fabsf(state->hpf_z[ch].z1) < 1e-15f) state->hpf_z[ch].z1 = 0.0f;
        if (!isfinite(state->hpf_z[ch].z2) || fabsf(state->hpf_z[ch].z2) < 1e-15f) state->hpf_z[ch].z2 = 0.0f;
        for (uint8_t i = 0; i < state->active_band_count; ++i) {
            uint8_t b = state->active_band_index[i];
            if (!isfinite(state->band_z[ch][b].z1) || fabsf(state->band_z[ch][b].z1) < 1e-15f) state->band_z[ch][b].z1 = 0.0f;
            if (!isfinite(state->band_z[ch][b].z2) || fabsf(state->band_z[ch][b].z2) < 1e-15f) state->band_z[ch][b].z2 = 0.0f;
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

static void advance_smoothing_state(eq_dsp_state_t *state) {
    float t;

    if (!state || state->smooth_remaining == 0) {
        return;
    }

    t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }

    state->preamp = lerp(state->preamp, state->target_preamp, t);
    for (int b = 0; b < EQ_BANDS; ++b) {
        if (state->active_band_enabled[b] || state->target_band_enabled[b]) {
            eq_biquad_t current;
            lerp_biquad(&state->active[b], &state->target[b], t, &current);
            state->active[b] = current;
            state->active_band_enabled[b] = 1;
        } else {
            biquad_identity(&state->active[b]);
            state->active_band_enabled[b] = 0;
        }
    }
    rebuild_band_index(state->active_band_enabled, state->active_band_index, &state->active_band_count);
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
    uint8_t next_index[EQ_BANDS];
    uint8_t next_count = 0;
    float next_preamp;
    int changed = 0;
    int sample_rate_changed = 0;
    int first_target;

    if (!state) {
        return;
    }

    sanitize_preamp_state(state);
    sanitize_smoothing_state(state);
    sanitize_biquad_state(state);
    first_target = !state->targets_initialized;

    sample_rate = normalize_sample_rate(sample_rate);
    if (sample_rate != state->sample_rate) {
        state->sample_rate = sample_rate;
        reset_delay_state(state);
        biquad_highpass(&state->hpf, state->sample_rate, 70);
        changed = 1;
        sample_rate_changed = 1;
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        int32_t gain_mdB = eq_clamp_mdB(band_mdB ? band_mdB[i] : 0);
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
    rebuild_band_index(next_enabled, next_index, &next_count);

    next_preamp = mdB_to_gain(eq_clamp_mdB(preamp_mdB));
    if (fabsf(state->target_preamp - next_preamp) > 0.000001f ||
        state->hpf_enabled != (hpf_enabled ? 1 : 0)) {
        changed = 1;
    }

    if (!changed) {
        state->targets_initialized = 1;
        return;
    }

    if (!first_target && !sample_rate_changed) {
        advance_smoothing_state(state);
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        state->target[i] = next_target[i];
        state->target_band_enabled[i] = next_enabled[i];
        if (first_target || sample_rate_changed) {
            state->active[i] = next_target[i];
            state->active_band_enabled[i] = next_enabled[i];
        }
    }
    memcpy(state->target_band_index, next_index, sizeof(next_index));
    state->target_band_count = next_count;
    if (first_target || sample_rate_changed) {
        memcpy(state->active_band_index, next_index, sizeof(next_index));
        state->active_band_count = next_count;
    } else {
        rebuild_band_index(state->active_band_enabled, state->active_band_index, &state->active_band_count);
    }

    state->target_preamp = next_preamp;
    if (first_target || sample_rate_changed) {
        state->preamp = next_preamp;
        state->smooth_remaining = 0;
    } else {
        state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    }
    if (state->hpf_enabled != (hpf_enabled ? 1 : 0)) {
        reset_hpf_delay_state(state);
    }
    state->hpf_enabled = hpf_enabled ? 1 : 0;
    state->targets_initialized = 1;
}

static inline float process_biquad(const eq_biquad_t *c, eq_biquad_delay_t *z, float x) {
    float y = c->b0 * x + z->z1;
    z->z1 = c->b1 * x - c->a1 * y + z->z2;
    z->z2 = c->b2 * x - c->a2 * y;
    return y;
}

static void process_stereo_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                  uint32_t frames, int32_t *clip_counter,
                                  uint16_t *peak_l, uint16_t *peak_r) {
    const uint8_t active_count = state->active_band_count;
    const uint8_t *active_index = state->active_band_index;
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    if (state->hpf_enabled) {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            float left = (float)input[0];
            float right = (float)input[1];

            left = process_biquad(&state->hpf, &state->hpf_z[0], left) * preamp;
            right = process_biquad(&state->hpf, &state->hpf_z[1], right) * preamp;

            for (uint8_t i = 0; i < active_count; ++i) {
                uint8_t b = active_index[i];
                left = process_biquad(&state->active[b], &state->band_z[0][b], left);
                right = process_biquad(&state->active[b], &state->band_z[1][b], right);
            }

            int16_t out_l = limit_i16(left, clip_counter);
            int16_t out_r = limit_i16(right, clip_counter);
            uint16_t abs_l = abs_i16_peak(out_l);
            uint16_t abs_r = abs_i16_peak(out_r);

            output[0] = out_l;
            output[1] = out_r;
            if (abs_l > max_l) max_l = abs_l;
            if (abs_r > max_r) max_r = abs_r;
            input += 2;
            output += 2;
        }
    } else {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            float left = (float)input[0] * preamp;
            float right = (float)input[1] * preamp;

            for (uint8_t i = 0; i < active_count; ++i) {
                uint8_t b = active_index[i];
                left = process_biquad(&state->active[b], &state->band_z[0][b], left);
                right = process_biquad(&state->active[b], &state->band_z[1][b], right);
            }

            int16_t out_l = limit_i16(left, clip_counter);
            int16_t out_r = limit_i16(right, clip_counter);
            uint16_t abs_l = abs_i16_peak(out_l);
            uint16_t abs_r = abs_i16_peak(out_r);

            output[0] = out_l;
            output[1] = out_r;
            if (abs_l > max_l) max_l = abs_l;
            if (abs_r > max_r) max_r = abs_r;
            input += 2;
            output += 2;
        }
    }

    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

static void process_generic_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                   uint32_t frames, uint32_t channels, int32_t *clip_counter,
                                   uint16_t *peak_l, uint16_t *peak_r) {
    const uint8_t active_count = state->active_band_count;
    const uint8_t *active_index = state->active_band_index;
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (frame * channels) + ch;
            float sample = (float)input[idx];

            if (state->hpf_enabled) {
                sample = process_biquad(&state->hpf, &state->hpf_z[ch], sample);
            }

            sample *= preamp;

            for (uint8_t i = 0; i < active_count; ++i) {
                uint8_t b = active_index[i];
                sample = process_biquad(&state->active[b], &state->band_z[ch][b], sample);
            }

            int16_t out_val = limit_i16(sample, clip_counter);
            uint16_t abs_val = abs_i16_peak(out_val);
            output[idx] = out_val;

            if (ch == 0) {
                if (abs_val > max_l) max_l = abs_val;
            } else if (ch == 1) {
                if (abs_val > max_r) max_r = abs_val;
            }
        }
    }

    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

void eq_dsp_apply_to(eq_dsp_state_t *state, const int16_t *input, int16_t *output, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    if (!state || !input || !output || channels < 1 || channels > EQ_DSP_MAX_CHANNELS) { return; }

    sanitize_preamp_state(state);
    sanitize_smoothing_state(state);
    sanitize_biquad_state(state);
    flush_denormal_delay_state(state, channels);

    if (state->smooth_remaining == 0) {
        if (channels == 2) {
            process_stereo_steady(state, input, output, frames, clip_counter, peak_l, peak_r);
        } else {
            process_generic_steady(state, input, output, frames, channels, clip_counter, peak_l, peak_r);
        }

        flush_denormal_delay_state(state, channels);
        return;
    }

    eq_biquad_t smooth_band[EQ_BANDS];
    uint8_t smooth_band_enabled[EQ_BANDS];
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        float t = 1.0f;
        int smoothing_now = (state->smooth_remaining > 0);

        if (smoothing_now) {
            t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
            memset(smooth_band_enabled, 0, sizeof(smooth_band_enabled));
            for (int b = 0; b < EQ_BANDS; ++b) {
                if (!state->active_band_enabled[b] && !state->target_band_enabled[b]) {
                    continue;
                }
                lerp_biquad(&state->active[b], &state->target[b], t, &smooth_band[b]);
                smooth_band_enabled[b] = 1;
            }
        }

        float preamp = (state->smooth_remaining > 0)
            ? lerp(state->preamp, state->target_preamp, t)
            : state->preamp;

        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (i * channels) + ch;
            float sample = (float)input[idx];
            
            // 1. Apply HPF if enabled
            if (state->hpf_enabled) {
                sample = process_biquad(&state->hpf, &state->hpf_z[ch], sample);
            }
            
            // 2. Apply Preamp
            sample *= preamp;

            // 3. Apply EQ Bands
            if (smoothing_now) {
                for (int b = 0; b < EQ_BANDS; ++b) {
                    if (!smooth_band_enabled[b]) {
                        continue;
                    }
                    sample = process_biquad(&smooth_band[b], &state->band_z[ch][b], sample);
                }
            } else {
                for (int b = 0; b < EQ_BANDS; ++b) {
                    if (state->active_band_enabled[b]) {
                        sample = process_biquad(&state->active[b], &state->band_z[ch][b], sample);
                    }
                }
            }

            // 4. Soft-knee limiter followed by integer clamping.
            int16_t out_val = limit_i16(sample, clip_counter);
            output[idx] = out_val;
            
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
                memcpy(state->active_band_index, state->target_band_index, sizeof(state->active_band_index));
                state->active_band_count = state->target_band_count;
                state->preamp = state->target_preamp;
            }
        }
    }

    flush_denormal_delay_state(state, channels);
    
    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    eq_dsp_apply_to(state, pcm, pcm, frames, channels, clip_counter, peak_l, peak_r);
}

uint32_t eq_dsp_active_band_count(const eq_dsp_state_t *state) {
    if (!state) {
        return 0;
    }

    return state->active_band_count;
}
