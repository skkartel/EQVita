#pragma once

#include <stdint.h>

#include "media_browser.h"

#ifndef EQVITA_HOST_TESTS
#include <psp2/kernel/threadmgr/lw_mutex.h>
#endif

#define EQVITA_MEDIA_PLAYER_VOLUME_MAX 100
#define EQVITA_MEDIA_PLAYER_DEFAULT_VOLUME 100

typedef enum eqvita_media_player_state
{
    EQVITA_MEDIA_PLAYER_STOPPED = 0,
    EQVITA_MEDIA_PLAYER_PLAYING,
    EQVITA_MEDIA_PLAYER_PAUSED,
    EQVITA_MEDIA_PLAYER_FINISHED,
    EQVITA_MEDIA_PLAYER_ERROR
} eqvita_media_player_state_t;

typedef enum eqvita_media_player_format
{
    EQVITA_MEDIA_FORMAT_NONE = 0,
    EQVITA_MEDIA_FORMAT_OGG,
    EQVITA_MEDIA_FORMAT_MP3,
    EQVITA_MEDIA_FORMAT_WAV
} eqvita_media_player_format_t;

typedef struct eqvita_media_player_status
{
    eqvita_media_player_state_t state;
    eqvita_media_player_format_t format;
    char path[EQVITA_MEDIA_MAX_PATH];
    uint32_t sample_rate;
    uint8_t channels;
    int volume;
    int loop;
    int last_error;
    uint32_t underrun_count;
    uint32_t ring_fill;
    uint32_t ring_capacity;
    uint32_t decode_max_us;
    uint32_t output_max_us;
} eqvita_media_player_status_t;

typedef struct eqvita_media_player
{
    eqvita_media_player_status_t status;
    volatile int stop_requested;
    volatile int pause_requested;
    int thread_id;
    int audio_port;
    void *decoder;
#ifndef EQVITA_HOST_TESTS
    SceKernelLwMutexWork status_mutex;
    int status_mutex_ready;
    int bgm_acquired;
#endif
} eqvita_media_player_t;

void eqvita_media_player_init(eqvita_media_player_t *player);
void eqvita_media_player_shutdown(eqvita_media_player_t *player);
int eqvita_media_player_open(eqvita_media_player_t *player, const char *path);
void eqvita_media_player_stop(eqvita_media_player_t *player);
void eqvita_media_player_toggle_pause(eqvita_media_player_t *player);
void eqvita_media_player_set_paused(eqvita_media_player_t *player, int paused);
void eqvita_media_player_set_loop(eqvita_media_player_t *player, int loop);
void eqvita_media_player_adjust_volume(eqvita_media_player_t *player, int delta);
eqvita_media_player_status_t eqvita_media_player_status(const eqvita_media_player_t *player);
const char *eqvita_media_player_state_label(eqvita_media_player_state_t state);
const char *eqvita_media_player_format_label(eqvita_media_player_format_t format);
