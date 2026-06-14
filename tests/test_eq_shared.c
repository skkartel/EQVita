#include "../common/eq_shared.h"

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

#define ASSERT_EQ_I32(actual, expected) do { \
    int32_t a_ = (int32_t)(actual); \
    int32_t e_ = (int32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %d, got %d\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

static void test_defaults_are_compatible_and_safe(void) {
    eq_control_t ctrl;
    memset(&ctrl, 0xA5, sizeof(ctrl));

    eq_control_init_defaults(&ctrl);

    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.size, sizeof(eq_control_t));
    ASSERT_EQ_U32(ctrl.enabled, 0);
    ASSERT_EQ_U32(ctrl.speaker_only, 1);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 1);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_SAFE);
    ASSERT_EQ_I32(ctrl.preamp_mdB, EQ_DEFAULT_PREAMP_MDB);
    ASSERT_TRUE(eq_control_is_compatible(&ctrl));
}

static void test_validation_rejects_wrong_abi(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_ABI_VERSION + 1;
    ASSERT_TRUE(eq_control_validate(&ctrl) < 0);

    eq_control_init_defaults(&ctrl);
    ctrl.size = sizeof(eq_control_t) - 1;
    ASSERT_TRUE(eq_control_validate(&ctrl) < 0);
}

static void test_validation_accepts_legacy_size_and_normalizes(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_LEGACY_ABI_VERSION_1_10;
    ctrl.size = sizeof(eq_shared_block_t);
    ctrl.enabled = 7;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;
    ctrl.band_gain_mdB[2] = EQ_MAX_ABS_GAIN_MDB + 1;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.size, sizeof(eq_control_t));
    ASSERT_EQ_U32(ctrl.enabled, 1);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[2], EQ_MAX_ABS_GAIN_MDB);
}

static void test_validation_accepts_1_11_control_files(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_LEGACY_ABI_VERSION_1_11;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
}

static void test_validation_normalizes_and_clamps(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.enabled = 9;
    ctrl.speaker_only = 3;
    ctrl.hpf_enabled = 2;
    ctrl.route_hint = 99;
    ctrl.preamp_mdB = EQ_MAX_ABS_GAIN_MDB + 5000;
    ctrl.band_gain_mdB[0] = -(EQ_MAX_ABS_GAIN_MDB + 1000);
    ctrl.band_gain_mdB[1] = EQ_MAX_ABS_GAIN_MDB + 1000;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.enabled, 1);
    ASSERT_EQ_U32(ctrl.speaker_only, 1);
    ASSERT_EQ_U32(ctrl.hpf_enabled, 1);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_I32(ctrl.preamp_mdB, EQ_MAX_ABS_GAIN_MDB);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[0], -EQ_MAX_ABS_GAIN_MDB);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[1], EQ_MAX_ABS_GAIN_MDB);
}

static void test_headroom_mode_is_encoded_without_changing_control_size(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ASSERT_EQ_U32(sizeof(ctrl), 60);

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_LOUD);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 1);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);

    eq_control_set_hpf_enabled(&ctrl, 0);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);
}

static void test_legacy_payload_resets_headroom_mode(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_RAW);
    ctrl.version = EQ_LEGACY_ABI_VERSION_1_12;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_SAFE);
}

static void test_route_hint_selection_persists_for_global_audio(void) {
    ASSERT_TRUE(!eq_route_hint_is_usable(EQ_ROUTE_UNKNOWN, 1, 0, 0));
    ASSERT_TRUE(!eq_route_hint_is_usable(EQ_ROUTE_SPEAKER, 0, 0, 0));
    ASSERT_TRUE(eq_route_hint_is_usable(EQ_ROUTE_SPEAKER, 2, 1, UINT32_MAX));
    ASSERT_TRUE(eq_route_hint_is_usable(EQ_ROUTE_BLUETOOTH, 2, 2, UINT32_MAX));

    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_SPEAKER, 2, 2, UINT32_MAX, 0), EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_SPEAKER, 2, 2, UINT32_MAX, 1), EQ_ROUTE_HEADPHONES);
    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_UNKNOWN, 2, 2, 0, 0), EQ_ROUTE_UNKNOWN);
}

static void test_legacy_preset_fallback_only_when_new_missing(void) {
    ASSERT_TRUE(eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_MISSING));
    ASSERT_TRUE(!eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_VALID));
    ASSERT_TRUE(!eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_INVALID));
}

static void test_preset_wrapper_round_trips_and_detects_corruption(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.dirty_counter = 77;
    ctrl.route_hint = EQ_ROUTE_BLUETOOTH;
    ctrl.band_gain_mdB[4] = 2500;

    eq_preset_build(&preset, &ctrl);
    ASSERT_TRUE(eq_preset_validate(&preset) == 0);
    ASSERT_TRUE(eq_preset_extract_control(&preset, &loaded) == 0);
    ASSERT_EQ_U32(loaded.dirty_counter, 0);
    ASSERT_EQ_U32(preset.control.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_UNKNOWN);

    preset.control.band_gain_mdB[4] = 3000;
    ASSERT_TRUE(eq_preset_validate(&preset) < 0);

    eq_preset_build(&preset, &ctrl);
    preset.magic ^= 0x10u;
    ASSERT_TRUE(eq_preset_validate(&preset) < 0);
}

static void test_preset_wrapper_imports_legacy_control_version(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.band_gain_mdB[1] = 4000;

    eq_preset_build(&preset, &ctrl);
    preset.control.version = EQ_LEGACY_ABI_VERSION_1_12;
    preset.checksum = eq_preset_checksum(&preset);

    ASSERT_TRUE(eq_preset_extract_control(&preset, &loaded) == 0);
    ASSERT_EQ_U32(loaded.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(loaded.enabled, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 4000);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_SAFE);
}

static void test_preset_rejects_checksum_valid_invalid_control(void) {
    eq_control_t ctrl;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 9;
    ctrl.band_gain_mdB[2] = EQ_MAX_ABS_GAIN_MDB + 1000;

    memset(&preset, 0, sizeof(preset));
    preset.magic = EQ_PRESET_MAGIC;
    preset.version = EQ_PRESET_VERSION;
    preset.header_size = (uint32_t)offsetof(eq_preset_file_t, control);
    preset.payload_size = (uint32_t)sizeof(eq_control_t);
    preset.band_count = EQ_PRESET_BAND_COUNT;
    preset.control = ctrl;
    preset.checksum = eq_preset_checksum(&preset);

    ASSERT_TRUE(eq_preset_validate(&preset) < 0);
}

static void test_boot_state_preserves_route_hint_and_round_trips(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_boot_state_file_t state;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;
    ctrl.band_gain_mdB[1] = 4000;
    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_LOUD);

    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_validate(&state) == 0);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(loaded.enabled, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 4000);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_LOUD);
}

static void test_boot_state_assumes_speaker_when_enabled_route_unknown(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_boot_state_file_t state;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.route_hint = EQ_ROUTE_UNKNOWN;

    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_SPEAKER);

    ctrl.enabled = 0;
    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_UNKNOWN);
}

int main(void) {
    test_defaults_are_compatible_and_safe();
    test_validation_rejects_wrong_abi();
    test_validation_accepts_legacy_size_and_normalizes();
    test_validation_accepts_1_11_control_files();
    test_validation_normalizes_and_clamps();
    test_headroom_mode_is_encoded_without_changing_control_size();
    test_legacy_payload_resets_headroom_mode();
    test_route_hint_selection_persists_for_global_audio();
    test_legacy_preset_fallback_only_when_new_missing();
    test_preset_wrapper_round_trips_and_detects_corruption();
    test_preset_wrapper_imports_legacy_control_version();
    test_preset_rejects_checksum_valid_invalid_control();
    test_boot_state_preserves_route_hint_and_round_trips();
    test_boot_state_assumes_speaker_when_enabled_route_unknown();

    if (failures) {
        printf("%d shared validation failure(s)\n", failures);
        return 1;
    }

    return 0;
}
