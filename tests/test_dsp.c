#include "../plugin/dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ_I32(actual, expected) do { \
    int32_t a_ = (int32_t)(actual); \
    int32_t e_ = (int32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %d, got %d\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

static void test_left_channel_filter_state_does_not_bleed_into_right(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[1024 * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    for (int i = 0; i < EQ_BANDS; ++i) {
        bands[i] = 0;
    }
    bands[4] = 9000;

    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);

    for (int i = 0; i < 1024; ++i) {
        pcm[i * 2] = 8000;
        pcm[(i * 2) + 1] = 0;
    }

    eq_dsp_apply(&dsp, pcm, 1024, 2, &clips, &peak_l, &peak_r);

    for (int i = 0; i < 1024; ++i) {
        if (pcm[(i * 2) + 1] != 0) {
            printf("right channel changed at frame %d: %d\n", i, pcm[(i * 2) + 1]);
            failures++;
            break;
        }
    }
    ASSERT_TRUE(peak_l > 0);
    ASSERT_EQ_I32(peak_r, 0);
}

static void test_negative_full_scale_peak_is_reported(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[4] = {-32768, -32768, 0, 0};
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 1.0f;
    dsp.target_preamp = 1.0f;
    dsp.smooth_remaining = 0;

    eq_dsp_apply(&dsp, pcm, 2, 2, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(peak_l, 32768);
    ASSERT_EQ_I32(peak_r, 32768);
}

static void test_overflow_counts_before_limiter(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[2] = {32767, -32768};
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 2.0f;
    dsp.target_preamp = 2.0f;
    dsp.smooth_remaining = 0;

    eq_dsp_apply(&dsp, pcm, 1, 2, &clips, &peak_l, &peak_r);

    ASSERT_TRUE(clips >= 2);
    ASSERT_TRUE(pcm[0] < 32767);
    ASSERT_TRUE(pcm[0] > 30000);
    ASSERT_TRUE(pcm[1] > -32768);
    ASSERT_TRUE(pcm[1] < -30000);
}

static void test_smoothing_reaches_target(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
}

static void test_flat_eq_does_not_keep_active_band_filters(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES * 2];
    int32_t clips = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 0);

    bands[4] = 6000;
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, &clips, NULL, NULL);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 1);

    bands[4] = 0;
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, &clips, NULL, NULL);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 0);
}

static void test_reapplying_same_targets_does_not_restart_smoothing(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES];
    int32_t clips = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);

    eq_dsp_set_targets(&dsp, 48000, bands, EQ_DEFAULT_PREAMP_MDB, 0);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    ASSERT_TRUE(dsp.smooth_remaining > 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, &clips, NULL, NULL);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
}

int main(void) {
    test_left_channel_filter_state_does_not_bleed_into_right();
    test_negative_full_scale_peak_is_reported();
    test_overflow_counts_before_limiter();
    test_smoothing_reaches_target();
    test_flat_eq_does_not_keep_active_band_filters();
    test_reapplying_same_targets_does_not_restart_smoothing();

    if (failures) {
        printf("%d dsp failure(s)\n", failures);
        return 1;
    }

    return 0;
}
