#include <psp2kernel/kernel/modulemgr.h>
#include <psp2kernel/kernel/threadmgr.h>
#include <psp2kernel/kernel/sysmem.h>
#include <psp2kernel/kernel/audiomacro.h>
#include <taihen.h>
#include <string.h>

#include "../../common/eq_shared.h"
#include "dsp.h"
#include "port_registry.h"
#include "port_state.h"

#define HOOKS_NUM 5

static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];

static eq_audio_port_registry_t registry;
static eq_control_t control;

static int audio_thread_uid = -1;
static int audio_thread_run = 1;

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

        if (!eq_audio_port_open(&processing_config)) {
            reason = EQ_BYPASS_PORT_CLOSED;
        } else if (!eq_audio_port_can_process(&processing_config, EQ_AUDIO_SCRATCH_MAX_FRAMES)) {
            reason = (frames > EQ_AUDIO_SCRATCH_MAX_FRAMES) ? EQ_BYPASS_BUFFER_TOO_LARGE : EQ_BYPASS_UNSUPPORTED_FORMAT;
        } else if (channels == 0 || channels > EQ_DSP_MAX_CHANNELS || frames > EQ_AUDIO_SCRATCH_MAX_FRAMES || ((uint64_t)frames * (uint64_t)channels * sizeof(int16_t)) > sizeof(processing_port->scratch)) {
            reason = EQ_BYPASS_UNSUPPORTED_FORMAT;
        } else if (!control.enabled) {
            reason = EQ_BYPASS_DISABLED;
        } else {
            processing_bytes = (size_t)frames * channels * sizeof(int16_t);

            ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes);

            eq_audio_port_set_config(&processing_config);
            eq_dsp_process(processing_port->scratch, processing_port->original, frames, eq_audio_mode_to_channels(processing_config.mode));

            ksceKernelCopyToUser((void *)buf, processing_port->scratch, processing_bytes);
        }

        processing_port->bypass_reason = reason;
        processing_port->state = EQ_PORT_STATE_PROCESSED;
        
        ksceKernelUnlockMutex(processing_port->mutex);
        ksceKernelSignalCond(processing_port->cond_processed);
    }

    return 0;
}

static int sceAudioOutOutput_hook(int port, const void *buf) {
    int ret = 0;
    eq_audio_tracked_port_t *tracked_port = eq_audio_port_registry_get(&registry, port);

    if (!tracked_port) {
        return TAI_CONTINUE(int, refs[0], port, buf);
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
    return TAI_CONTINUE(int, refs[0], port, buf);
}

static int sceAudioOutOpenPort_hook(int type, int len, int freq, int mode) {
    int port = TAI_CONTINUE(int, refs[1], type, len, freq, mode);
    if (port >= 0) {
        eq_audio_port_config_t config = { .type = type, .len = len, .freq = freq, .mode = mode, .channels = eq_audio_mode_to_channels(mode) };
        eq_audio_port_registry_add(&registry, port, &config);
    }
    return port;
}

static int sceAudioOutReleasePort_hook(int port) {
    int ret = TAI_CONTINUE(int, refs[2], port);
    if (ret == 0) {
        eq_audio_port_registry_remove(&registry, port);
    }
    return ret;
}

static int sceAudioOutSetConfig_hook(int port, int len, int freq, int mode) {
    int ret = TAI_CONTINUE(int, refs[3], port, len, freq, mode);
    if (ret == 0) {
        eq_audio_port_config_t config = { .len = len, .freq = freq, .mode = mode, .channels = eq_audio_mode_to_channels(mode) };
        eq_audio_port_registry_update_config(&registry, port, &config);
    }
    return ret;
}

static int sceAudioOutGetConfig_hook(int port, int type, int *val) {
    int ret = TAI_CONTINUE(int, refs[4], port, type, val);
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

    hooks[0] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[0], "SceAudio", 0x438BB957, 0x02DB3F5F, sceAudioOutOutput_hook);
    hooks[1] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[1], "SceAudio", 0x438BB957, 0x5BC341E4, sceAudioOutOpenPort_hook);
    hooks[2] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[2], "SceAudio", 0x438BB957, 0xB8BA0D07, sceAudioOutReleasePort_hook);
    hooks[3] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[3], "SceAudio", 0x438BB957, 0x9C8EDAEA, sceAudioOutSetConfig_hook);
    hooks[4] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[4], "SceAudio", 0x438BB957, 0x69E2E6B5, sceAudioOutGetConfig_hook);

    return SCE_KERNEL_START_SUCCESS;
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
    return SCE_KERNEL_STOP_SUCCESS;
}
