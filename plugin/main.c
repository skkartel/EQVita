#include <taihen.h>
#include <stdint.h>
#include <stddef.h>

#include "../common/eq_shared.h"
#include "dsp.h"
#include "port_registry.h"

#define HOOKS_NUM 5

static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];

static eq_audio_port_registry_t registry;
static eq_control_t control;

void *audio_lock_branch = (void*)1;

static int sceAudioOutOutput_hook(int port, const void *buf)
{
    eq_audio_tracked_port_t *tracked_port;
    uint32_t frames;
    uint32_t channels;
    size_t sample_count;
    size_t processing_bytes;
    size_t scratch_capacity;
    int copy_in;
    int copy_out;

    if (!buf) {
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    tracked_port = eq_audio_port_registry_begin_processing(&registry, port);

    if (!tracked_port) {
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    frames = tracked_port->config.len;
    channels = tracked_port->config.channels;

    /*
     * Maximum number of int16 samples physically available in
     * original[] and scratch[].
     */
    scratch_capacity =
        (size_t)EQ_AUDIO_SCRATCH_MAX_FRAMES *
        (size_t)EQ_DSP_MAX_CHANNELS;

    /*
     * Reject invalid dimensions before doing any multiplication.
     */
    if (channels == 0 ||
        channels > EQ_DSP_MAX_CHANNELS ||
        frames == 0 ||
        frames > EQ_AUDIO_SCRATCH_MAX_FRAMES) {

        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    /*
     * Overflow-safe frames * channels calculation.
     */
    if ((size_t)frames > SIZE_MAX / (size_t)channels) {
        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    sample_count = (size_t)frames * (size_t)channels;

    /*
     * Absolute multi-channel bounds defense:
     *
     * frames * channels must fit inside:
     *
     * EQ_AUDIO_SCRATCH_MAX_FRAMES * EQ_DSP_MAX_CHANNELS
     */
    if (sample_count > scratch_capacity) {
        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    /*
     * Protect sample_count -> byte-count conversion.
     */
    if (sample_count > SIZE_MAX / sizeof(int16_t)) {
        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    processing_bytes = sample_count * sizeof(int16_t);

    /*
     * Confirm the calculated transfer fits both real arrays in
     * eq_audio_tracked_port_t.
     */
    if (processing_bytes > sizeof(tracked_port->original) ||
        processing_bytes > sizeof(tracked_port->scratch)) {

        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    /*
     * EQ disabled: leave the original audio path untouched.
     */
    if (!control.enabled) {
        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    /*
     * Copy incoming PCM into the tracked port's kernel-side buffer.
     */
    copy_in = ksceKernelCopyFromUser(
        tracked_port->original,
        buf,
        processing_bytes
    );

    if (copy_in < 0) {
        eq_audio_port_registry_end_processing(tracked_port);
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    /*
     * Run the actual DSP using the tracked port's persistent DSP state.
     */
    eq_dsp_apply_to(
        &tracked_port->dsp,
        tracked_port->original,
        tracked_port->scratch,
        frames,
        channels,
        NULL,
        NULL,
        NULL
    );

    /*
     * Copy processed PCM back to SceAudio's output buffer.
     */
    copy_out = ksceKernelCopyToUser(
        (void *)buf,
        tracked_port->scratch,
        processing_bytes
    );

    eq_audio_port_registry_end_processing(tracked_port);

    if (copy_out < 0) {
        return TAI_CONTINUE(int, refs[0], port, buf);
    }

    return 0;
}

static int sceAudioOutOpenPort_hook(
    int type,
    int len,
    int freq,
    int mode
)
{
    int port;

    port = TAI_CONTINUE(
        int,
        refs[1],
        type,
        len,
        freq,
        mode
    );

    if (port >= 0) {
        eq_audio_port_registry_open(
            &registry,
            port,
            (uint32_t)type,
            (uint32_t)len,
            (uint32_t)freq,
            mode
        );
    }

    return port;
}

static int sceAudioOutReleasePort_hook(int port)
{
    int ret;

    ret = TAI_CONTINUE(
        int,
        refs[2],
        port
    );

    if (ret == 0) {
        eq_audio_port_registry_release(
            &registry,
            port
        );
    }

    return ret;
}

static int sceAudioOutSetConfig_hook(
    int port,
    int len,
    int freq,
    int mode
)
{
    int ret;

    ret = TAI_CONTINUE(
        int,
        refs[3],
        port,
        len,
        freq,
        mode
    );

    if (ret == 0) {
        eq_audio_port_registry_set_config(
            &registry,
            port,
            (uint32_t)len,
            freq,
            mode
        );
    }

    return ret;
}

static int sceAudioOutGetConfig_hook(
    int port,
    int type,
    int *val
)
{
    return TAI_CONTINUE(
        int,
        refs[4],
        port,
        type,
        val
    );
}

void _start(void) __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args)
{
    tai_module_info_t info;

    (void)argc;
    (void)args;

    info.size = sizeof(tai_module_info_t);

    eq_audio_port_registry_init(&registry);

    control.enabled = 1;

    hooks[0] = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &refs[0],
        "SceAudio",
        0x438BB957,
        0x02DB3F5F,
        sceAudioOutOutput_hook
    );

    hooks[1] = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &refs[1],
        "SceAudio",
        0x438BB957,
        0x5BC341E4,
        sceAudioOutOpenPort_hook
    );

    hooks[2] = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &refs[2],
        "SceAudio",
        0x438BB957,
        0xB8BA0D07,
        sceAudioOutReleasePort_hook
    );

    hooks[3] = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &refs[3],
        "SceAudio",
        0x438BB957,
        0x9C8EDAEA,
        sceAudioOutSetConfig_hook
    );

    hooks[4] = taiHookFunctionExportForKernel(
        KERNEL_PID,
        &refs[4],
        "SceAudio",
        0x438BB957,
        0x69E2E6B5,
        sceAudioOutGetConfig_hook
    );

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args)
{
    (void)argc;
    (void)args;

    for (int i = 0; i < HOOKS_NUM; ++i) {
        if (hooks[i] >= 0) {
            taiHookReleaseForKernel(
                hooks[i],
                refs[i]
            );

            hooks[i] = -1;
        }
    }

    return SCE_KERNEL_STOP_SUCCESS;
}
