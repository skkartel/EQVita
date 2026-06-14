#include "app_state.h"

#include <string.h>

static int normalize_slot(int slot)
{
    if (slot < 0 || slot >= EQVITA_PRESET_SLOT_COUNT) {
        return 0;
    }
    return slot;
}

void eqvita_app_state_init(eqvita_app_state_t *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    eq_control_init_defaults(&state->control);
    state->preset_slot = 0;
    state->plugin_compatible = 1;
    state->last_set_result = 0;
    state->last_status_result = 0;
}

int eqvita_app_state_preset_slot(const eqvita_app_state_t *state)
{
    return state ? state->preset_slot : 0;
}

void eqvita_app_state_set_preset_slot(eqvita_app_state_t *state, int slot)
{
    if (!state) {
        return;
    }
    state->preset_slot = normalize_slot(slot);
}

void eqvita_app_state_adjust_preset_slot(eqvita_app_state_t *state, int delta)
{
    int slot;

    if (!state) {
        return;
    }

    slot = state->preset_slot + delta;
    while (slot < 0) {
        slot += EQVITA_PRESET_SLOT_COUNT;
    }
    while (slot >= EQVITA_PRESET_SLOT_COUNT) {
        slot -= EQVITA_PRESET_SLOT_COUNT;
    }
    state->preset_slot = slot;
}

eq_control_t *eqvita_app_state_begin_edit(eqvita_app_state_t *state)
{
    if (!state) {
        return NULL;
    }

    state->draft_control = state->control;
    state->draft_active = 1;
    return &state->draft_control;
}

void eqvita_app_state_commit_edit(eqvita_app_state_t *state)
{
    if (!state || !state->draft_active) {
        return;
    }

    state->control = state->draft_control;
    state->draft_active = 0;
    eqvita_app_state_mark_boot_dirty(state);
    eqvita_app_state_mark_current_preset_dirty(state);
}

void eqvita_app_state_rollback_edit(eqvita_app_state_t *state)
{
    if (!state) {
        return;
    }

    memset(&state->draft_control, 0, sizeof(state->draft_control));
    state->draft_active = 0;
}

void eqvita_app_state_set_control(eqvita_app_state_t *state, const eq_control_t *control)
{
    if (!state || !control) {
        return;
    }

    state->control = *control;
    state->draft_active = 0;
    memset(&state->draft_control, 0, sizeof(state->draft_control));
}

void eqvita_app_state_mark_boot_dirty(eqvita_app_state_t *state)
{
    if (state) {
        state->boot_dirty = 1;
    }
}

void eqvita_app_state_mark_boot_saved(eqvita_app_state_t *state)
{
    if (state) {
        state->boot_dirty = 0;
    }
}

int eqvita_app_state_boot_dirty(const eqvita_app_state_t *state)
{
    return state ? state->boot_dirty != 0 : 0;
}

void eqvita_app_state_mark_current_preset_dirty(eqvita_app_state_t *state)
{
    if (state && state->preset_slot >= 0 && state->preset_slot < EQVITA_PRESET_SLOT_COUNT) {
        state->preset_dirty[state->preset_slot] = 1;
    }
}

void eqvita_app_state_mark_current_preset_saved(eqvita_app_state_t *state)
{
    if (state && state->preset_slot >= 0 && state->preset_slot < EQVITA_PRESET_SLOT_COUNT) {
        state->preset_dirty[state->preset_slot] = 0;
    }
}

int eqvita_app_state_current_preset_dirty(const eqvita_app_state_t *state)
{
    if (!state || state->preset_slot < 0 || state->preset_slot >= EQVITA_PRESET_SLOT_COUNT) {
        return 0;
    }
    return state->preset_dirty[state->preset_slot] != 0;
}

void eqvita_app_state_set_status_stale(eqvita_app_state_t *state, int stale)
{
    if (state) {
        state->status_stale = stale ? 1u : 0u;
    }
}

int eqvita_app_state_status_stale(const eqvita_app_state_t *state)
{
    return state ? state->status_stale != 0 : 0;
}
