#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/ctrl.h>
#include <taihen.h>
#include <psp2/audioout.h>
#include <string.h>

#include "dsp.h"
#include "port_registry.h"
#include "../common/eq_shared.h"

// Diagnostic build flag: when set to 1, skip all initialization.
#define DIAG_MINIMAL 0

static SceUID g_state_mutex = -1;
static SceUID g_audio_mutex = -1;

static eq_audio_port_registry_t g_ports;
static eq_control_t g_control;
static eq_status_t g_status;
static volatile int g_processing_enabled;
static volatile int g_active_callbacks;
static volatile int g_unloading;
static uint32_t g_route_hint_last_counter;
static uint32_t g_route_hint_stale_buffers;

static tai_hook_ref_t g_hook_output;
static tai_hook_ref_t g_hook_open;
static tai_hook_ref_t g_hook_set_config;
static tai_hook_ref_t g_hook_get_config;
static tai_hook_ref_t g_hook_release;
static SceUID g_hook_id_output = -1;
static SceUID g_hook_id_open = -1;
static SceUID g_hook_id_set_config = -1;
static SceUID g_hook_id_get_config = -1;
static SceUID g_hook_id_release = -1;

static void dsp_reset_identity(void) {
    // Individual ports are reset on open.
}

static int lock_state(void) {
    if (g_state_mutex < 0) {
        return -1;
    }
    return ksceKernelLockMutex(g_state_mutex, 1, NULL);
}

static int try_lock_state(void) {
    if (g_state_mutex < 0) {
        return -1;
    }
    return ksceKernelTryLockMutex(g_state_mutex, 1);
}

static void unlock_state(void) {
    if (g_state_mutex >= 0) {
        ksceKernelUnlockMutex(g_state_mutex, 1);
    }
}

static int lock_audio(void) {
    if (g_audio_mutex < 0) {
        return -1;
    }
    return ksceKernelLockMutex(g_audio_mutex, 1, NULL);
}

static int try_lock_audio(void) {
    if (g_audio_mutex < 0) {
        return -1;
    }
    return ksceKernelTryLockMutex(g_audio_mutex, 1);
}

static void unlock_audio(void) {
    if (g_audio_mutex >= 0) {
        ksceKernelUnlockMutex(g_audio_mutex, 1);
    }
}

static int hook_enter(void) {
    __sync_add_and_fetch(&g_active_callbacks, 1);
    return !g_unloading;
}

static void hook_leave(void) {
    __sync_sub_and_fetch(&g_active_callbacks, 1);
}

static eq_route_t detect_route(const eq_control_t *control, uint32_t last_counter, uint32_t stale_buffers) {
    int wired_headphones_connected = 0;

    uint8_t route_hint = control ? control->route_hint : EQ_ROUTE_UNKNOWN;
    uint32_t counter = control ? control->dirty_counter : 0;

    /*
     * The app can see AVConfig route information that this kernel plugin cannot.
     * Still prefer the kernel headphone bit so a persisted speaker hint cannot
     * keep applying speaker EQ after the user inserts wired headphones.
     */
    SceCtrlData data;
    memset(&data, 0, sizeof(data));
    if (ksceCtrlPeekBufferPositive(0, &data, 1) >= 0 && (data.buttons & SCE_CTRL_HEADPHONE)) {
        wired_headphones_connected = 1;
    }

    return (eq_route_t)eq_route_select(route_hint, counter, last_counter, stale_buffers, wired_headphones_connected);
}

#include <psp2kern/io/fcntl.h>

#define BOOT_STATE_PATH "ur0:data/eqvita/boot.eqbs"
#define PRESET_PATH "ur0:data/eqvita/preset0.eqvp"
#define LEGACY_PRESET_PATH "ur0:data/eqvita/preset0.bin"

static int load_boot_state_kernel(void) {
    SceUID fd = ksceIoOpen(BOOT_STATE_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_boot_state_file_t state;
        eq_control_t loaded;
        int r = ksceIoRead(fd, &state, sizeof(state));
        ksceIoClose(fd);
        if (r == sizeof(state) && eq_boot_state_extract_control(&state, &loaded) == 0) {
            g_control = loaded;
            g_control.dirty_counter++;
            return 0;
        }
    }

    return -1;
}

static void load_preset_kernel(void) {
    eq_preset_primary_status_t primary_status = EQ_PRESET_PRIMARY_MISSING;
    SceUID fd;

    if (load_boot_state_kernel() == 0) {
        return;
    }

    fd = ksceIoOpen(PRESET_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_preset_file_t preset;
        eq_control_t loaded;
        int r = ksceIoRead(fd, &preset, sizeof(preset));
        ksceIoClose(fd);
        if (r == sizeof(preset) && eq_preset_extract_control(&preset, &loaded) == 0) {
            primary_status = EQ_PRESET_PRIMARY_VALID;
            eq_control_prepare_for_boot(&loaded);
            g_control = loaded;
            g_control.dirty_counter++;
            return;
        }
        primary_status = EQ_PRESET_PRIMARY_INVALID;
    }

    if (!eq_preset_should_try_legacy(primary_status)) {
        return;
    }

    fd = ksceIoOpen(LEGACY_PRESET_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_control_t tmp;
        int r = ksceIoRead(fd, &tmp, sizeof(tmp));
        ksceIoClose(fd);
        if (r == sizeof(tmp) && eq_control_validate(&tmp) == 0) {
            eq_control_prepare_for_boot(&tmp);
            g_control = tmp;
            g_control.dirty_counter++;
        }
    }
}

static void set_defaults(void) {
    memset(&g_status, 0, sizeof(g_status));
    eq_control_init_defaults(&g_control);
    
    // Try to load preset
    load_preset_kernel();

    g_status.sample_rate = 48000;
    g_status.route = EQ_ROUTE_UNKNOWN;
    g_status.eq_active = 0;
    g_status.bypass_reason = EQ_BYPASS_DISABLED;
}

// No shared mem block needed; app uses syscalls to set/get control and status.

static void update_dsp_if_needed(eq_audio_tracked_port_t *port, const eq_control_t *control, eq_route_t route) {
    if (!port || !port->config.in_use || !control) return;
    
    uint32_t dirty = control->dirty_counter;
    if (dirty != port->last_dirty || port->config.freq != port->dsp.sample_rate || port->last_route != (uint8_t)route) {
        int32_t band_mdB[EQ_BANDS];
        for (int i = 0; i < EQ_BANDS; ++i) {
            band_mdB[i] = control->band_gain_mdB[i];
        }

        if (route == EQ_ROUTE_SPEAKER) {
            // Do not excite the 31 Hz band on speakers; fold it into 62 Hz and zero it out
            int32_t merged = band_mdB[1] + band_mdB[0];
            if (merged > EQ_MAX_ABS_GAIN_MDB) merged = EQ_MAX_ABS_GAIN_MDB;
            if (merged < -EQ_MAX_ABS_GAIN_MDB) merged = -EQ_MAX_ABS_GAIN_MDB;
            band_mdB[1] = merged;
            band_mdB[0] = 0;
        }

        int32_t max_boost = 0;
        for (int i = 0; i < EQ_BANDS; ++i) {
            if (band_mdB[i] > max_boost) {
                max_boost = band_mdB[i];
            }
        }

        uint8_t headroom_mode = eq_control_get_headroom_mode(control);
        int32_t effective_preamp = control->preamp_mdB;
        if (headroom_mode == EQ_HEADROOM_LOUD) {
            int32_t makeup = max_boost / 2;
            if (makeup > 3000) makeup = 3000;
            effective_preamp += makeup;
        } else if (headroom_mode == EQ_HEADROOM_RAW) {
            effective_preamp = 0;
        }
        if (effective_preamp > EQ_MAX_ABS_GAIN_MDB) effective_preamp = EQ_MAX_ABS_GAIN_MDB;
        if (effective_preamp < -EQ_MAX_ABS_GAIN_MDB) effective_preamp = -EQ_MAX_ABS_GAIN_MDB;

        int hpf_enabled = (route == EQ_ROUTE_SPEAKER) ? 1 : eq_control_hpf_enabled(control);

        eq_dsp_set_targets(&port->dsp, port->config.freq, band_mdB, effective_preamp, hpf_enabled);
        port->last_dirty = dirty;
        port->last_route = (uint8_t)route;
    }
}

static void update_status(uint32_t sample_rate, eq_route_t route, int eq_active, int smoothing, eq_bypass_reason_t reason) {
    g_status.sample_rate = sample_rate;
    g_status.route = (uint8_t)route;
    g_status.eq_active = (uint8_t)eq_active;
    g_status.smoothing = (uint8_t)smoothing;
    g_status.bypass_reason = (uint8_t)reason;
    g_status.status_counter++;
}

static int recover_port_config_locked(int port) {
    int len;
    int freq;
    int mode;

    if (g_hook_id_get_config < 0) {
        return -1;
    }

    len = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
    freq = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
    mode = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);

    if (len <= 0 || freq <= 0 || mode < 0) {
        return -1;
    }

    return eq_audio_port_registry_open(&g_ports, port, 0, (uint32_t)len, (uint32_t)freq, mode) ? 0 : -1;
}

static int sceAudioOutGetConfig_hook(int port, int type) {
    int res;
    if (!hook_enter()) {
        hook_leave();
        return SCE_AUDIO_OUT_ERROR_BUSY;
    }

    res = TAI_CONTINUE(int, g_hook_get_config, port, type);
    hook_leave();
    return res;
}

static int sceAudioOutOutput_hook(int port, const void *buf) {
    uint32_t sample_rate = 48000;
    uint32_t channels = 0;
    uint32_t frames = 0;
    uint32_t active_ports = 0;
    uint32_t start_us = 0;
    uint32_t elapsed_us = 0;
    eq_route_t route = EQ_ROUTE_UNKNOWN;
    eq_bypass_reason_t reason = EQ_BYPASS_INVALID_PORT;
    int applied = 0;
    int clip_count = 0;
    int smoothing = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;
    eq_control_t control;
    eq_audio_tracked_port_t *processing_port = NULL;
    eq_audio_port_config_t processing_config;
    uint32_t route_last_counter = 0;
    uint32_t route_stale_buffers = EQ_ROUTE_HINT_MAX_STALE_BUFFERS;
    int state_ready = 0;
    int ret;

    start_us = ksceKernelGetSystemTimeLow();

    if (!hook_enter()) {
        hook_leave();
        return SCE_AUDIO_OUT_ERROR_BUSY;
    }

    if (!g_processing_enabled || !buf) {
        ret = TAI_CONTINUE(int, g_hook_output, port, buf);
        hook_leave();
        return ret;
    }

    memset(&control, 0, sizeof(control));

    if (try_lock_state() >= 0) {
        control = g_control;
        route_last_counter = g_route_hint_last_counter;
        if (control.dirty_counter != g_route_hint_last_counter) {
            g_route_hint_last_counter = control.dirty_counter;
            g_route_hint_stale_buffers = 0;
            route_stale_buffers = 0;
        } else {
            if (g_route_hint_stale_buffers < UINT32_MAX) {
                g_route_hint_stale_buffers++;
            }
            route_stale_buffers = g_route_hint_stale_buffers;
        }
        state_ready = 1;
        unlock_state();
    }

    if (!state_ready) {
        reason = EQ_BYPASS_AUDIO_BUSY;
    } else if (try_lock_audio() >= 0) {
        eq_audio_tracked_port_t *p = eq_audio_port_registry_find(&g_ports, port);
        if (!p) {
            (void)recover_port_config_locked(port);
            p = eq_audio_port_registry_find(&g_ports, port);
        }

        if (p && p->processing) {
            reason = EQ_BYPASS_AUDIO_BUSY;
        } else if (p && p->config.in_use) {
            processing_port = eq_audio_port_registry_begin_processing(&g_ports, port);
            if (processing_port) {
                processing_config = processing_port->config;
            }
        }

        active_ports = eq_audio_port_registry_count(&g_ports);
        unlock_audio();
    } else {
        reason = EQ_BYPASS_AUDIO_BUSY;
    }

    if (processing_port) {
        sample_rate = processing_config.freq;
        channels = processing_config.channels;
        frames = processing_config.len;

        if (!eq_audio_port_can_process(&processing_config, EQ_AUDIO_SCRATCH_MAX_FRAMES)) {
            reason = (frames > EQ_AUDIO_SCRATCH_MAX_FRAMES) ? EQ_BYPASS_BUFFER_TOO_LARGE : EQ_BYPASS_UNSUPPORTED_FORMAT;
        } else if (!control.enabled) {
            reason = EQ_BYPASS_DISABLED;
        } else {
            size_t bytes = (size_t)frames * channels * sizeof(int16_t);

            route = detect_route(&control, route_last_counter, route_stale_buffers);
            if (route == EQ_ROUTE_UNKNOWN) {
                reason = EQ_BYPASS_UNKNOWN_ROUTE;
            } else if (control.speaker_only && route != EQ_ROUTE_SPEAKER) {
                reason = EQ_BYPASS_SPEAKER_ONLY;
            } else {
                reason = EQ_BYPASS_NONE;
                if (ksceKernelCopyFromUser(processing_port->scratch, buf, bytes) >= 0) {
                    if (processing_port->dsp.sample_rate == 0) {
                        eq_dsp_init(&processing_port->dsp, sample_rate);
                    }

                    update_dsp_if_needed(processing_port, &control, route);

                    eq_dsp_apply(&processing_port->dsp, processing_port->scratch, frames, channels, &clip_count, &peak_l, &peak_r);

                    smoothing = (processing_port->dsp.smooth_remaining > 0);

                    if (ksceKernelCopyToUser((void *)buf, processing_port->scratch, bytes) >= 0) {
                        applied = 1;
                    } else {
                        reason = EQ_BYPASS_COPY_FAILED;
                    }
                } else {
                    reason = EQ_BYPASS_COPY_FAILED;
                }
            }
        }

        for (int unlock_retry = 0; unlock_retry < 100; ++unlock_retry) {
            if (lock_audio() >= 0) {
                eq_audio_port_registry_end_processing(processing_port);
                active_ports = eq_audio_port_registry_count(&g_ports);
                unlock_audio();
                break;
            }
            ksceKernelDelayThread(100);
        }
    }

    elapsed_us = ksceKernelGetSystemTimeLow() - start_us;

    if (try_lock_state() >= 0) {
        if (applied) {
            if (peak_l > g_status.peak_l) g_status.peak_l = peak_l;
            if (peak_r > g_status.peak_r) g_status.peak_r = peak_r;
            if (clip_count > 0) {
                g_status.clip_events += clip_count;
            }
        }

        g_status.debug_port = (uint32_t)port;
        g_status.debug_len = frames;
        g_status.debug_channels = channels;
        g_status.debug_run_count++;
        g_status.debug_active_ports = active_ports;
        g_status.debug_last_us = elapsed_us;
        if (elapsed_us > g_status.debug_max_us) {
            g_status.debug_max_us = elapsed_us;
        }
        if (reason == EQ_BYPASS_AUDIO_BUSY) {
            g_status.debug_busy_bypass_count++;
        } else if (reason == EQ_BYPASS_INVALID_PORT) {
            g_status.debug_unknown_port_count++;
        }

        update_status(sample_rate, route, applied, smoothing, reason);
        unlock_state();
    }

    ret = TAI_CONTINUE(int, g_hook_output, port, buf);
    hook_leave();
    return ret;
}

static int sceAudioOutOpenPort_hook(int type, int len, int freq, int mode) {
    if (!hook_enter()) {
        hook_leave();
        return SCE_AUDIO_OUT_ERROR_BUSY;
    }
    int port = TAI_CONTINUE(int, g_hook_open, type, len, freq, mode);
    if (g_processing_enabled && port >= 0) {
        if (lock_audio() >= 0) {
            (void)eq_audio_port_registry_open(&g_ports, port, (uint32_t)type, (uint32_t)len, (uint32_t)freq, mode);
            unlock_audio();
        }
    }
    hook_leave();
    return port;
}

static int sceAudioOutSetConfig_hook(int port, SceSize len, int freq, int mode) {
    if (!hook_enter()) {
        hook_leave();
        return SCE_AUDIO_OUT_ERROR_BUSY;
    }
    int res = TAI_CONTINUE(int, g_hook_set_config, port, len, freq, mode);
    if (g_processing_enabled && port >= 0 && res >= 0) {
        if (lock_audio() >= 0) {
            uint32_t len_arg = (len == (SceSize)-1) ? EQ_AUDIO_KEEP_U32 : (uint32_t)len;
            (void)eq_audio_port_registry_set_config(&g_ports, port, len_arg, freq, mode);
            unlock_audio();
        }
    }
    hook_leave();
    return res;
}

static int sceAudioOutReleasePort_hook(int port) {
    if (!hook_enter()) {
        hook_leave();
        return SCE_AUDIO_OUT_ERROR_BUSY;
    }
    int res = TAI_CONTINUE(int, g_hook_release, port);
    if (g_processing_enabled && port >= 0 && res >= 0) {
        if (lock_audio() >= 0) {
            (void)eq_audio_port_registry_release(&g_ports, port);
            unlock_audio();
        }
    }
    hook_leave();
    return res;
}

void EqGetVersion(eq_version_t *out) {
    if (!out) { return; }
    eq_version_t v = {EQ_VERSION_MAJOR, EQ_VERSION_MINOR, EQ_VERSION_PATCH, 0};
    ksceKernelCopyToUser((void *)out, &v, sizeof(v));
}

int EqSetControl(const eq_control_t *user_ctrl) {
    if (!user_ctrl) { return -1; }
    eq_control_t tmp;
    if (ksceKernelCopyFromUser(&tmp, user_ctrl, sizeof(tmp)) < 0) {
        return -1;
    }
    if (eq_control_validate(&tmp) < 0) {
        return -2;
    }
    if (lock_state() < 0) {
        return -3;
    }
    tmp.dirty_counter = g_control.dirty_counter + 1;
    g_control = tmp;
    unlock_state();
    return 0;
}

int EqGetStatus(eq_status_t *out_status) {
    if (!out_status) { return -1; }
    eq_status_t tmp;
    if (lock_state() < 0) {
        return -2;
    }
    tmp = g_status;
    // Reset peaks after a read so the next UI frame reports fresh max values.
    g_status.peak_l = 0;
    g_status.peak_r = 0;
    unlock_state();
    return ksceKernelCopyToUser(out_status, &tmp, sizeof(tmp));
}

static int wait_for_hooks_to_idle(void) {
    for (int i = 0; i < 100; ++i) {
        if (g_active_callbacks <= 0) {
            return 0;
        }
        ksceKernelDelayThread(1000);
    }
    return (g_active_callbacks <= 0) ? 0 : -1;
}

static int release_hook_if_installed(SceUID *hook_id, tai_hook_ref_t hook_ref) {
    if (!hook_id || *hook_id < 0) {
        return 0;
    }

    int res = taiHookReleaseForKernel(*hook_id, hook_ref);
    if (res < 0) {
        return res;
    }

    *hook_id = -1;
    return 0;
}

static int cleanup(void) {
    g_processing_enabled = 0;
    g_unloading = 1;

    if (wait_for_hooks_to_idle() < 0) {
        g_unloading = 0;
        return -1;
    }

    int release_res = 0;
    if (release_hook_if_installed(&g_hook_id_output, g_hook_output) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_open, g_hook_open) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_set_config, g_hook_set_config) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_get_config, g_hook_get_config) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_release, g_hook_release) < 0) { release_res = -1; }
    if (release_res < 0) {
        g_unloading = 0;
        return -1;
    }

    if (wait_for_hooks_to_idle() < 0) {
        g_unloading = 0;
        return -1;
    }

    if (g_audio_mutex >= 0) { ksceKernelDeleteMutex(g_audio_mutex); g_audio_mutex = -1; }
    if (g_state_mutex >= 0) { ksceKernelDeleteMutex(g_state_mutex); g_state_mutex = -1; }
    return 0;
}

int _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *argv) {
    (void)argc; (void)argv;

#if DIAG_MINIMAL
    // Minimal diagnostic: do nothing, just signal success.
    return SCE_KERNEL_START_SUCCESS;
#else
    dsp_reset_identity();
    set_defaults();
    eq_audio_port_registry_init(&g_ports);
    
    g_processing_enabled = 0;
    g_active_callbacks = 0;
    g_unloading = 0;
    g_route_hint_last_counter = 0;
    g_route_hint_stale_buffers = EQ_ROUTE_HINT_MAX_STALE_BUFFERS;
    g_state_mutex = ksceKernelCreateMutex("eq_state_mutex", 0, 0, NULL);
    if (g_state_mutex < 0) {
        return SCE_KERNEL_START_FAILED;
    }
    g_audio_mutex = ksceKernelCreateMutex("eq_audio_mutex", 0, 0, NULL);
    if (g_audio_mutex < 0) {
        (void)cleanup();
        return SCE_KERNEL_START_FAILED;
    }

    g_hook_id_output = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_output, "SceAudio", 0x438BB957, 0x02DB3F5F, sceAudioOutOutput_hook);
    g_hook_id_open = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_open, "SceAudio", 0x438BB957, 0x5BC341E4, sceAudioOutOpenPort_hook);
    g_hook_id_set_config = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_set_config, "SceAudio", 0x438BB957, 0xB8BA0D07, sceAudioOutSetConfig_hook);
    g_hook_id_get_config = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_get_config, "SceAudio", 0x438BB957, 0x9C8EDAEA, sceAudioOutGetConfig_hook);
    g_hook_id_release = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_release, "SceAudio", 0x438BB957, 0x69E2E6B5, sceAudioOutReleasePort_hook);

    if (g_hook_id_output < 0 || g_hook_id_open < 0 || g_hook_id_set_config < 0 || g_hook_id_get_config < 0 || g_hook_id_release < 0) {
        (void)cleanup();
        return SCE_KERNEL_START_FAILED;
    }

    g_processing_enabled = 1;
    return SCE_KERNEL_START_SUCCESS;
#endif
}

int module_stop(SceSize argc, const void *argv) {
    (void)argc; (void)argv;
    return (cleanup() == 0) ? SCE_KERNEL_STOP_SUCCESS : SCE_KERNEL_STOP_FAIL;
}
