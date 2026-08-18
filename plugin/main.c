#include <taihen.h>
#include <stdint.h>

#include "../common/eq_shared.h"
#include "dsp.h"
#include "port_registry.h"

#define HOOKS_NUM 5

static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];

static eq_audio_port_registry_t registry;
static eq_control_t control;

static int audio_thread_uid = -1;
static int audio_thread_run = 1;

void *audio_lock_branch = (void*)1;

static int audio_thread(SceSize args, void *argp) {
    eq_audio_tracked_port_t *processing_port = NULL;
    eq_audio_port_config_t processing_config;
    size_t processing_bytes = 0;
    uint32_t channels = 0;
    uint32_t frames = 0;
    uint32_t sample_rate = 0;
    uint32_t reason = 0;
    void *buf = NULL;

    while (audio_thread_run) {
        processing_port = eq_audio_port_registry_get_next_processing(&registry);
        if (!processing_port) {
            ksceKernelDelayThread(1000);
            continue;
        }

        buf = processing_port->user_buffer;
        reason = EQ_BYPASS_NONE;

        ksceKernelLockMutex(processing_port->mutex, 1, NULL);
        
        processing_config = processing_port->pending_config;
        processing_port->config = processing_config;

        sample_rate = processing_config.freq;
        channels = processing_config.channels;
        frames = processing_config.len;

        processing_bytes = (size_t)frames * (size_t)channels * sizeof(int16_t);

        if (channels == 0 ||
            frames == 0 ||
            channels > (SIZE_MAX / sizeof(int16_t)) ||
            frames > (SIZE_MAX / ((size_t)channels * sizeof(int16_t))) ||
            processing_bytes > sizeof(processing_port->scratch) ||
            processing_bytes > sizeof(processing_port->original)) {

            processing_bytes = 0;
            reason = EQ_BYPASS_BUFFER_TOO_LARGE;
            ksceKernelUnlockMutex(processing_port->mutex);
        } else if (!control.enabled) {
            reason = EQ_BYPASS_DISABLED;
            ksceKernelUnlockMutex(processing_port->mutex);
        } else {
            /* Safely copy user memory inside the lock */
            int copy_in = ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes);
            
            /* CRITICAL FIX: Unlock the mutex BEFORE running the heavy DSP math */
            /* This stops the plugin from blocking the SD2Vita storage driver thread */
            ksceKernelUnlockMutex(processing_port->mutex);

            if (copy_in >= 0) {
                eq_dsp_process(processing_port->scratch, processing_port->original, frames, channels);
                ksceKernelCopyToUser((void *)buf, processing_port->scratch, processing_bytes);
            } else {
                reason = EQ_BYPASS_BUFFER_TOO_LARGE;
            }
        }

        processing_port->bypass_reason = reason;
        processing_port->state = EQ_PORT_STATE_PROCESSED;
        
        ksceKernelSignalCond(processing_port->cond_processed);
    }

    return 0;
}

static int sceAudioOutOutput_hook(int port, const void *buf) {
    eq_audio_tracked_port_t *tracked_port = eq_audio_port_registry_get(&registry, port);

    if (!tracked_port) {
        return TAI_CONTINUE(int, refs, port, buf);
    }

    ksceKernelLockMutex(tracked_port->mutex, 1, NULL);

    if (tracked_port->state == EQ_PORT_STATE_FREE) {
        tracked_port->user_buffer = (void *)buf;
        tracked_port->state = EQ_PORT_STATE_READY;
        
        eq_audio_port_registry_push_processing(&registry, tracked_port);

        while (tracked_port->state != EQ_PORT_STATE_PROCESSED) {
            ksceKernelWaitCond(tracked_port->cond_processed, NULL);
        }

        tracked_port->state = EQ_PORT_STATE_FREE;
        ksceKernelUnlockMutex(tracked_port->mutex);
        
        return 0;
    }

    ksceKernelUnlockMutex(tracked_port->mutex);
    return TAI_CONTINUE(int, refs, port, buf);
}

static int sceAudioOutOpenPort_hook(int type, int len, int freq, int mode) {
    int port = TAI_CONTINUE(int, refs, type, len, freq, mode);
    if (port >= 0) {
        eq_audio_port_config_t config = { .type = type, .len = len, .freq = freq, .mode = mode, .channels = (mode == 0 ? 1 : 2) };
        eq_audio_port_registry_add(&registry, port, &config);
    }
    return port;
}

static int sceAudioOutReleasePort_hook(int port) {
    int ret = TAI_CONTINUE(int, refs, port);
    if (ret == 0) {
        eq_audio_port_registry_remove(&registry, port);
    }
    return ret;
}

static int sceAudioOutSetConfig_hook(int port, int len, int freq, int mode) {
    int ret = TAI_CONTINUE(int, refs, port, len, freq, mode);
    if (ret == 0) {
        eq_audio_port_config_t config = { .len = len, .freq = freq, .mode = mode, .channels = (mode == 0 ? 1 : 2) };
        eq_audio_port_registry_update_config(&registry, port, &config);
    }
    return ret;
}

static int sceAudioOutGetConfig_hook(int port, int type, int *val) {
    int ret = TAI_CONTINUE(int, refs, port, type, val);
    if (ret == 0 && type == 0) {
        eq_audio_port_registry_sync_len(&registry, port, val);
    }
    return ret;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
    tai_module_info_t info;
    info.size = sizeof(tai_module_info_t);

    eq_audio_port_registry_init(&registry);
    eq_dsp_init();

    control.enabled = 1;

    audio_thread_run = 1;
    audio_thread_uid = ksceKernelCreateThread("eq_audio_thread", audio_thread, 0x3C, 0x4000, 0, 0, NULL);
    if (audio_thread_uid >= 0) {
        ksceKernelStartThread(audio_thread_uid, 0, NULL);
    }

    hooks = taiHookFunctionExportForKernel(KERNEL_PID, &refs, "SceAudio", 0x438BB957, 0x02DB3F5F, sceAudioOutOutput_hook);
    hooks = taiHookFunctionExportForKernel(KERNEL_PID, &refs, "SceAudio", 0x438BB957, 0x5BC341E4, sceAudioOutOpenPort_hook);
    hooks = taiHookFunctionExportForKernel(KERNEL_PID, &refs, "SceAudio", 0x438BB957, 0xB8BA0D07, sceAudioOutReleasePort_hook);
    hooks = taiHookFunctionExportForKernel(KERNEL_PID, &refs, "SceAudio", 0x438BB957, 0x9C8EDAEA, sceAudioOutSetConfig_hook);
    hooks = taiHookFunctionExportForKernel(KERNEL_PID, &refs, "SceAudio", 0x438BB957, 0x69E2E6B5, sceAudioOutGetConfig_hook);

    return TAI_CONTINUE(int, refs, 0);
}

int module_stop(SceSize argc, const void *args) {
    audio_thread_run = 0;
    if (audio_thread_uid >= 0) {
        ksceKernelWaitThreadEnd(audio_thread_uid, NULL, NULL);
        ksceKernelDeleteThread(audio_thread_uid);
    }

    for (int i = 0; i < HOOKS_NUM; i++) {
        if (hooks[i] >= 0) taiHookReleaseForKernel(hooks[i], refs[i]);
    }

    eq_audio_port_registry_destroy(&registry);
    return TAI_CONTINUE(int, refs, 0);
}
