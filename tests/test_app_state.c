#include "../app/app_state.h"

#include <stdint.h>
#include <stdio.h>

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

static void test_defaults_are_safe_and_slot_zero(void)
{
    eqvita_app_state_t state;
    eqvita_app_state_init(&state);

    ASSERT_EQ_I32(state.control.enabled, 0);
    ASSERT_EQ_I32(state.control.speaker_only, 1);
    ASSERT_EQ_I32(eqvita_app_state_preset_slot(&state), 0);
    ASSERT_TRUE(!eqvita_app_state_boot_dirty(&state));
    ASSERT_TRUE(!eqvita_app_state_current_preset_dirty(&state));
    ASSERT_TRUE(!eqvita_app_state_status_stale(&state));
}

static void test_preset_slot_wraps(void)
{
    eqvita_app_state_t state;
    eqvita_app_state_init(&state);

    eqvita_app_state_adjust_preset_slot(&state, -1);
    ASSERT_EQ_I32(eqvita_app_state_preset_slot(&state), 2);

    eqvita_app_state_adjust_preset_slot(&state, 1);
    ASSERT_EQ_I32(eqvita_app_state_preset_slot(&state), 0);

    eqvita_app_state_set_preset_slot(&state, 99);
    ASSERT_EQ_I32(eqvita_app_state_preset_slot(&state), 0);
}

static void test_candidate_commit_replaces_control(void)
{
    eqvita_app_state_t state;
    eq_control_t *draft;
    eqvita_app_state_init(&state);

    draft = eqvita_app_state_begin_edit(&state);
    ASSERT_TRUE(draft != NULL);
    draft->enabled = 1;
    draft->band_gain_mdB[1] = 4000;
    eqvita_app_state_commit_edit(&state);

    ASSERT_EQ_I32(state.control.enabled, 1);
    ASSERT_EQ_I32(state.control.band_gain_mdB[1], 4000);
    ASSERT_TRUE(eqvita_app_state_boot_dirty(&state));
    ASSERT_TRUE(eqvita_app_state_current_preset_dirty(&state));
}

static void test_candidate_rollback_leaves_control_unchanged(void)
{
    eqvita_app_state_t state;
    eq_control_t *draft;
    eqvita_app_state_init(&state);

    draft = eqvita_app_state_begin_edit(&state);
    ASSERT_TRUE(draft != NULL);
    draft->enabled = 1;
    draft->band_gain_mdB[1] = 4000;
    eqvita_app_state_rollback_edit(&state);

    ASSERT_EQ_I32(state.control.enabled, 0);
    ASSERT_EQ_I32(state.control.band_gain_mdB[1], 0);
    ASSERT_TRUE(!eqvita_app_state_boot_dirty(&state));
    ASSERT_TRUE(!eqvita_app_state_current_preset_dirty(&state));
}

static void test_dirty_flags_can_be_marked_saved(void)
{
    eqvita_app_state_t state;
    eqvita_app_state_init(&state);
    eqvita_app_state_mark_boot_dirty(&state);
    eqvita_app_state_mark_current_preset_dirty(&state);

    ASSERT_TRUE(eqvita_app_state_boot_dirty(&state));
    ASSERT_TRUE(eqvita_app_state_current_preset_dirty(&state));

    eqvita_app_state_mark_boot_saved(&state);
    eqvita_app_state_mark_current_preset_saved(&state);

    ASSERT_TRUE(!eqvita_app_state_boot_dirty(&state));
    ASSERT_TRUE(!eqvita_app_state_current_preset_dirty(&state));
}

int main(void)
{
    test_defaults_are_safe_and_slot_zero();
    test_preset_slot_wraps();
    test_candidate_commit_replaces_control();
    test_candidate_rollback_leaves_control_unchanged();
    test_dirty_flags_can_be_marked_saved();
    return failures ? 1 : 0;
}
