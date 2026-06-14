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

int main(void) {
    test_set_config_minus_one_keeps_existing_values();
    test_unknown_or_oversized_buffers_are_not_processable();
    test_invalid_modes_are_rejected();

    if (failures) {
        printf("%d port-state failure(s)\n", failures);
        return 1;
    }

    return 0;
}
