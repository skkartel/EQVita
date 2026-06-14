#pragma once

#include <stdint.h>
#include <string.h>

#include "dsp.h"
#include "port_state.h"

#define EQ_AUDIO_MAX_TRACKED_PORTS 8
#define EQ_AUDIO_PORT_ID_UNUSED (-1)
#define EQ_AUDIO_SCRATCH_MAX_FRAMES 4096

typedef struct eq_audio_tracked_port
{
    int port_id;
    eq_audio_port_config_t config;
    eq_dsp_state_t dsp;
    uint32_t last_dirty;
    uint8_t last_route;
    uint8_t processing;
    uint8_t release_pending;
    uint8_t pending_config_valid;
    uint8_t reserved0;
    eq_audio_port_config_t pending_config;
    int16_t scratch[EQ_AUDIO_SCRATCH_MAX_FRAMES * EQ_DSP_MAX_CHANNELS];
} eq_audio_tracked_port_t;

typedef struct eq_audio_port_registry
{
    eq_audio_tracked_port_t slots[EQ_AUDIO_MAX_TRACKED_PORTS];
} eq_audio_port_registry_t;

static inline void eq_audio_tracked_port_reset(eq_audio_tracked_port_t *slot)
{
    if (!slot) {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->port_id = EQ_AUDIO_PORT_ID_UNUSED;
    slot->last_route = EQ_ROUTE_UNKNOWN;
}

static inline void eq_audio_port_registry_init(eq_audio_port_registry_t *registry)
{
    if (!registry) {
        return;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        eq_audio_tracked_port_reset(&registry->slots[i]);
    }
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_find(eq_audio_port_registry_t *registry, int port_id)
{
    if (!registry) {
        return NULL;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (registry->slots[i].port_id == port_id && registry->slots[i].config.in_use && !registry->slots[i].release_pending) {
            return &registry->slots[i];
        }
    }

    return NULL;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_alloc(eq_audio_port_registry_t *registry, int port_id)
{
    eq_audio_tracked_port_t *existing = eq_audio_port_registry_find(registry, port_id);
    if (existing) {
        return existing;
    }

    if (!registry) {
        return NULL;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (!registry->slots[i].config.in_use && !registry->slots[i].processing) {
            eq_audio_tracked_port_reset(&registry->slots[i]);
            registry->slots[i].port_id = port_id;
            return &registry->slots[i];
        }
    }

    return NULL;
}

static inline void eq_audio_tracked_port_apply_config(eq_audio_tracked_port_t *slot,
                                                      const eq_audio_port_config_t *next_config)
{
    uint32_t old_freq;
    uint8_t old_channels;

    if (!slot || !next_config || !next_config->in_use) {
        return;
    }

    old_freq = slot->config.freq;
    old_channels = slot->config.channels;
    slot->config = *next_config;

    if (slot->config.freq != old_freq || slot->config.channels != old_channels || slot->dsp.sample_rate == 0) {
        eq_dsp_init(&slot->dsp, slot->config.freq);
        slot->last_dirty = 0;
        slot->last_route = EQ_ROUTE_UNKNOWN;
    }
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_open(eq_audio_port_registry_t *registry,
                                                                   int port_id,
                                                                   uint32_t type,
                                                                   uint32_t len,
                                                                   uint32_t freq,
                                                                   int mode)
{
    eq_audio_tracked_port_t *slot = eq_audio_port_registry_alloc(registry, port_id);
    if (!slot) {
        return NULL;
    }

    if (eq_audio_port_open(&slot->config, type, len, freq, mode) < 0) {
        eq_audio_tracked_port_reset(slot);
        return NULL;
    }

    eq_dsp_init(&slot->dsp, freq);
    slot->last_dirty = 0;
    slot->last_route = EQ_ROUTE_UNKNOWN;
    return slot;
}

static inline int eq_audio_port_registry_set_config(eq_audio_port_registry_t *registry,
                                                    int port_id,
                                                    uint32_t len,
                                                    int freq,
                                                    int mode)
{
    eq_audio_tracked_port_t *slot = eq_audio_port_registry_find(registry, port_id);
    eq_audio_port_config_t next_config;

    if (!slot) {
        return -1;
    }

    next_config = slot->config;
    if (eq_audio_port_set_config(&next_config, len, freq, mode) < 0) {
        if (slot->processing) {
            slot->config.in_use = 0;
            slot->release_pending = 1;
        } else {
            eq_audio_tracked_port_reset(slot);
        }
        return -1;
    }

    if (slot->processing) {
        slot->pending_config = next_config;
        slot->pending_config_valid = 1;
        return 0;
    }

    eq_audio_tracked_port_apply_config(slot, &next_config);
    return 0;
}

static inline int eq_audio_port_registry_release(eq_audio_port_registry_t *registry, int port_id)
{
    eq_audio_tracked_port_t *slot = eq_audio_port_registry_find(registry, port_id);
    if (!slot) {
        return -1;
    }

    if (slot->processing) {
        slot->config.in_use = 0;
        slot->release_pending = 1;
        return 0;
    }

    eq_audio_tracked_port_reset(slot);
    return 0;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_begin_processing(eq_audio_port_registry_t *registry, int port_id)
{
    eq_audio_tracked_port_t *slot = eq_audio_port_registry_find(registry, port_id);
    if (!slot || slot->processing) {
        return NULL;
    }

    slot->processing = 1;
    return slot;
}

static inline void eq_audio_port_registry_end_processing(eq_audio_tracked_port_t *slot)
{
    if (!slot) {
        return;
    }

    slot->processing = 0;
    if (slot->release_pending) {
        eq_audio_tracked_port_reset(slot);
    } else if (slot->pending_config_valid) {
        eq_audio_port_config_t next_config = slot->pending_config;
        slot->pending_config_valid = 0;
        memset(&slot->pending_config, 0, sizeof(slot->pending_config));
        eq_audio_tracked_port_apply_config(slot, &next_config);
    }
}

static inline uint32_t eq_audio_port_registry_count(const eq_audio_port_registry_t *registry)
{
    uint32_t count = 0;

    if (!registry) {
        return 0;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (registry->slots[i].config.in_use) {
            count++;
        }
    }

    return count;
}
