#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQVITA_SOURCE_DIR
#define EQVITA_SOURCE_DIR "."
#endif

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *data;

    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    ASSERT_TRUE(size >= 0);
    ASSERT_TRUE(fseek(f, 0, SEEK_SET) == 0);
    data = (char *)malloc((size_t)size + 1u);
    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(fread(data, 1, (size_t)size, f) == (size_t)size);
    data[size] = '\0';
    fclose(f);
    return data;
}

static void test_media_player_has_explicit_shutdown_and_port_release(void)
{
    char path[512];
    char *source;
    char *header;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/media_player.h", EQVITA_SOURCE_DIR);
    header = read_file(path);

    ASSERT_TRUE(strstr(header, "eqvita_media_player_shutdown") != NULL);
    ASSERT_TRUE(strstr(header, "eqvita_media_player_stop") != NULL);
    ASSERT_TRUE(strstr(source, "sceAudioOutReleasePort") != NULL);
    ASSERT_TRUE(strstr(source, "sceAppMgrAcquireBgmPort") != NULL);
    ASSERT_TRUE(strstr(source, "sceAppMgrReleaseBgmPort") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelWaitThreadEnd") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_close_decoder") != NULL);

    free(source);
    free(header);
}

static void test_media_player_uses_safe_status_snapshot(void)
{
    char path[512];
    char *source;
    char *header;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/media_player.h", EQVITA_SOURCE_DIR);
    header = read_file(path);

    ASSERT_TRUE(strstr(header, "status_mutex") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_lock") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_unlock") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_set_state") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelCreateLwMutex") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelDeleteLwMutex") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelLockLwMutex") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelUnlockLwMutex") != NULL);

    free(source);
    free(header);
}

static void test_media_player_does_not_talk_to_plugin_control_plane(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "EqSetControl") == NULL);
    ASSERT_TRUE(strstr(source, "EqGetStatus") == NULL);
    ASSERT_TRUE(strstr(source, "EqDrainDiagnostics") == NULL);
    ASSERT_TRUE(strstr(source, "eq_control_t") == NULL);

    free(source);
}

static void test_media_player_supports_expected_decoder_libraries(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "vorbis/vorbisfile.h") != NULL);
    ASSERT_TRUE(strstr(source, "mpg123.h") != NULL);
    ASSERT_TRUE(strstr(source, "sndfile.h") == NULL);
    ASSERT_TRUE(strstr(source, "read_wav_header") != NULL);
    ASSERT_TRUE(strstr(source, "SCE_AUDIO_OUT_PORT_TYPE_BGM") != NULL);

    free(source);
}

static void test_media_player_keeps_audio_thread_hot_path_light(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "eqvita_media_player_set_state_if_changed") != NULL);
    ASSERT_TRUE(strstr(source, "decoder_zero_tail") != NULL);
    ASSERT_TRUE(strstr(source, "if (volume != EQVITA_MEDIA_PLAYER_VOLUME_MAX)") != NULL);
    ASSERT_TRUE(strstr(source, "memset(out, 0, (size_t)want_bytes)") == NULL);

    free(source);
}

static void test_media_player_uses_single_worker_for_preview_audio(void)
{
    char path[512];
    char *source;
    char *worker_thread;
    char *worker_thread_end;
    char *decode_call;
    char *output_call;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "PREVIEW_RING_CHUNKS") == NULL);
    ASSERT_TRUE(strstr(source, "preview_pcm_chunk_t") == NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_ring_push") == NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_ring_pop") == NULL);
    ASSERT_TRUE(strstr(source, "decode_thread_id") == NULL);
    ASSERT_TRUE(strstr(source, "audio_thread_id") == NULL);
    ASSERT_TRUE(strstr(source, "preview_decode_thread") == NULL);
    ASSERT_TRUE(strstr(source, "preview_audio_thread") == NULL);
    ASSERT_TRUE(strstr(source, "underrun_count") != NULL);
    ASSERT_TRUE(strstr(source, "decode_max_us") != NULL);
    ASSERT_TRUE(strstr(source, "output_max_us") != NULL);

    worker_thread = strstr(source, "static int player_thread");
    ASSERT_TRUE(worker_thread != NULL);
    worker_thread_end = strstr(worker_thread + 1, "void eqvita_media_player_init");
    ASSERT_TRUE(worker_thread_end != NULL);
    decode_call = strstr(worker_thread, "decoder_read(");
    output_call = strstr(worker_thread, "sceAudioOutOutput");
    ASSERT_TRUE(decode_call != NULL);
    ASSERT_TRUE(output_call != NULL);
    ASSERT_TRUE(decode_call < output_call);
    ASSERT_TRUE(output_call < worker_thread_end);

    free(source);
}

static void test_media_player_shutdown_has_bounded_output_fallback(void)
{
    char path[512];
    char *source;
    char *stop_fn;
    char *stop_end;
    char *release_call;
    char *wait_call;

    snprintf(path, sizeof(path), "%s/app/media_player.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "eqvita_media_player_wait_or_delete_thread") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelDeleteThread") != NULL);

    stop_fn = strstr(source, "void eqvita_media_player_stop");
    ASSERT_TRUE(stop_fn != NULL);
    stop_end = strstr(stop_fn + 1, "void eqvita_media_player_shutdown");
    ASSERT_TRUE(stop_end != NULL);
    release_call = strstr(stop_fn, "sceAudioOutReleasePort(player->audio_port)");
    wait_call = strstr(stop_fn, "eqvita_media_player_wait_or_delete_thread(player->thread_id");
    ASSERT_TRUE(release_call != NULL);
    ASSERT_TRUE(wait_call != NULL);
    ASSERT_TRUE(wait_call < stop_end);

    free(source);
}

int main(void)
{
    test_media_player_has_explicit_shutdown_and_port_release();
    test_media_player_uses_safe_status_snapshot();
    test_media_player_does_not_talk_to_plugin_control_plane();
    test_media_player_supports_expected_decoder_libraries();
    test_media_player_keeps_audio_thread_hot_path_light();
    test_media_player_uses_single_worker_for_preview_audio();
    test_media_player_shutdown_has_bounded_output_fallback();
    return 0;
}
