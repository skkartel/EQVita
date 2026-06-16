#include "../plugin/port_state.h"

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

#define ASSERT_EQ_U32(actual, expected) do { \
    uint32_t a_ = (uint32_t)(actual); \
    uint32_t e_ = (uint32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %u, got %u\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

static void test_set_config_minus_one_keeps_existing_values(void) {
    eq_audio_port_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) == 0);
    ASSERT_TRUE(eq_audio_port_set_config(&cfg, EQ_AUDIO_KEEP_U32, -1, -1) == 0);

    ASSERT_EQ_U32(cfg.len, 2048);
    ASSERT_EQ_U32(cfg.freq, 48000);
    ASSERT_EQ_U32(cfg.channels, 2);
    ASSERT_TRUE(eq_audio_port_can_process(&cfg, 4096));
}

static void test_unknown_or_oversized_buffers_are_not_processable(void) {
    eq_audio_port_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_TRUE(!eq_audio_port_can_process(&cfg, 4096));

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 8192, 48000, EQ_AUDIO_MODE_STEREO) == 0);
    ASSERT_TRUE(!eq_audio_port_can_process(&cfg, 4096));

    ASSERT_TRUE(eq_audio_port_set_config(&cfg, 2048, -1, -1) == 0);
    ASSERT_TRUE(eq_audio_port_can_process(&cfg, 4096));
}

static void test_invalid_modes_are_rejected(void) {
    eq_audio_port_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 48000, 99) < 0);
    ASSERT_TRUE(!cfg.in_use);

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 48000, EQ_AUDIO_MODE_MONO) == 0);
    ASSERT_EQ_U32(cfg.channels, 1);

    ASSERT_TRUE(eq_audio_port_set_config(&cfg, EQ_AUDIO_KEEP_U32, -1, 99) < 0);
    ASSERT_EQ_U32(cfg.channels, 1);
}

static void test_documented_lengths_are_enforced(void) {
    eq_audio_port_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 64, 48000, EQ_AUDIO_MODE_STEREO) == 0);
    ASSERT_TRUE(eq_audio_port_can_process(&cfg, 4096));

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 32, 48000, EQ_AUDIO_MODE_STEREO) < 0);
    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 65, 48000, EQ_AUDIO_MODE_STEREO) < 0);
    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 65473, 48000, EQ_AUDIO_MODE_STEREO) < 0);

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) == 0);
    ASSERT_TRUE(eq_audio_port_set_config(&cfg, 65, -1, -1) < 0);
    ASSERT_EQ_U32(cfg.len, 256);
}

static void test_documented_sample_rates_are_enforced(void) {
    eq_audio_port_config_t cfg;
    static const uint32_t supported[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
    };
    memset(&cfg, 0, sizeof(cfg));

    for (uint32_t i = 0; i < (uint32_t)(sizeof(supported) / sizeof(supported[0])); ++i) {
        ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, supported[i], EQ_AUDIO_MODE_STEREO) == 0);
        ASSERT_EQ_U32(cfg.freq, supported[i]);
    }

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 96000, EQ_AUDIO_MODE_STEREO) < 0);
    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 12345, EQ_AUDIO_MODE_STEREO) < 0);

    ASSERT_TRUE(eq_audio_port_open(&cfg, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) == 0);
    ASSERT_TRUE(eq_audio_port_set_config(&cfg, EQ_AUDIO_KEEP_U32, 96000, -1) < 0);
    ASSERT_EQ_U32(cfg.freq, 48000);
}

int main(void) {
    test_set_config_minus_one_keeps_existing_values();
    test_unknown_or_oversized_buffers_are_not_processable();
    test_invalid_modes_are_rejected();
    test_documented_lengths_are_enforced();
    test_documented_sample_rates_are_enforced();

    if (failures) {
        printf("%d port-state failure(s)\n", failures);
        return 1;
    }

    return 0;
}
