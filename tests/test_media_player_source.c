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

int main(void)
{
    test_media_player_has_explicit_shutdown_and_port_release();
    test_media_player_uses_safe_status_snapshot();
    test_media_player_does_not_talk_to_plugin_control_plane();
    test_media_player_supports_expected_decoder_libraries();
    return 0;
}
