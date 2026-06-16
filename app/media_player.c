#include "media_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQVITA_HOST_TESTS
#include <psp2/appmgr.h>
#include <mpg123.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <vorbis/vorbisfile.h>
#endif

#define PREVIEW_FRAMES 1024
#define PREVIEW_THREAD_PRIORITY 0x60
#define PREVIEW_THREAD_STACK (64 * 1024)

typedef struct eqvita_media_decoder
{
    eqvita_media_player_format_t format;
    uint32_t sample_rate;
    uint8_t channels;
    int eof;
#ifndef EQVITA_HOST_TESTS
    SceUID ogg_fd;
    OggVorbis_File ogg;
    int ogg_open;
    int ogg_section;
    mpg123_handle *mp3;
    SceUID wav_fd;
    uint32_t wav_data_start;
    uint32_t wav_data_bytes;
    uint32_t wav_bytes_read;
#endif
} eqvita_media_decoder_t;

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void eqvita_media_player_lock(const eqvita_media_player_t *player)
{
#ifndef EQVITA_HOST_TESTS
    if (player && player->status_mutex_ready) {
        sceKernelLockLwMutex((SceKernelLwMutexWork *)&player->status_mutex, 1, NULL);
    }
#else
    (void)player;
#endif
}

static void eqvita_media_player_unlock(const eqvita_media_player_t *player)
{
#ifndef EQVITA_HOST_TESTS
    if (player && player->status_mutex_ready) {
        sceKernelUnlockLwMutex((SceKernelLwMutexWork *)&player->status_mutex, 1);
    }
#else
    (void)player;
#endif
}

static void eqvita_media_player_set_state(eqvita_media_player_t *player, eqvita_media_player_state_t state)
{
    if (!player) {
        return;
    }
    eqvita_media_player_lock(player);
    player->status.state = state;
    eqvita_media_player_unlock(player);
}

static void eqvita_media_player_set_error(eqvita_media_player_t *player, int error)
{
    if (!player) {
        return;
    }
    eqvita_media_player_lock(player);
    player->status.last_error = error;
    player->status.state = EQVITA_MEDIA_PLAYER_ERROR;
    eqvita_media_player_unlock(player);
}

static int eqvita_media_player_command_snapshot(eqvita_media_player_t *player,
                                                int *paused,
                                                int *loop,
                                                int *volume)
{
    int stop;

    if (!player) {
        return 1;
    }

    eqvita_media_player_lock(player);
    stop = player->stop_requested;
    if (paused) {
        *paused = player->pause_requested;
    }
    if (loop) {
        *loop = player->status.loop;
    }
    if (volume) {
        *volume = player->status.volume;
    }
    eqvita_media_player_unlock(player);

    return stop;
}

static eqvita_media_player_format_t format_from_path(const char *path)
{
    const char *ext = path ? strrchr(eqvita_media_browser_file_name(path), '.') : NULL;
    if (!ext || !ext[1]) {
        return EQVITA_MEDIA_FORMAT_NONE;
    }
    ext++;
    if ((ext[0] == 'o' || ext[0] == 'O') &&
        (ext[1] == 'g' || ext[1] == 'G') &&
        (ext[2] == 'g' || ext[2] == 'G') &&
        ext[3] == '\0') {
        return EQVITA_MEDIA_FORMAT_OGG;
    }
    if ((ext[0] == 'm' || ext[0] == 'M') &&
        (ext[1] == 'p' || ext[1] == 'P') &&
        (ext[2] == '3') &&
        ext[3] == '\0') {
        return EQVITA_MEDIA_FORMAT_MP3;
    }
    if ((ext[0] == 'w' || ext[0] == 'W') &&
        (ext[1] == 'a' || ext[1] == 'A') &&
        (ext[2] == 'v' || ext[2] == 'V') &&
        ext[3] == '\0') {
        return EQVITA_MEDIA_FORMAT_WAV;
    }
    return EQVITA_MEDIA_FORMAT_NONE;
}

static int sample_rate_supported(uint32_t rate)
{
    switch (rate) {
        case 8000:
        case 11025:
        case 12000:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
            return 1;
        default:
            return 0;
    }
}

#ifndef EQVITA_HOST_TESTS
static size_t ogg_read_cb(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    SceUID fd = *(SceUID *)datasource;
    int ret = sceIoRead(fd, ptr, (unsigned int)(size * nmemb));
    return ret > 0 ? (size_t)ret : 0;
}

static int ogg_seek_cb(void *datasource, ogg_int64_t offset, int whence)
{
    SceUID fd = *(SceUID *)datasource;
    SceOff ret = sceIoLseek(fd, (SceOff)offset, whence);
    return ret < 0 ? -1 : 0;
}

static int ogg_close_cb(void *datasource)
{
    SceUID fd = *(SceUID *)datasource;
    if (fd >= 0) {
        sceIoClose(fd);
    }
    return 0;
}

static long ogg_tell_cb(void *datasource)
{
    SceUID fd = *(SceUID *)datasource;
    SceOff ret = sceIoLseek(fd, 0, SCE_SEEK_CUR);
    return ret < 0 ? -1 : (long)ret;
}

static int open_ogg(eqvita_media_decoder_t *decoder, const char *path)
{
    ov_callbacks callbacks;
    vorbis_info *info;

    decoder->ogg_fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (decoder->ogg_fd < 0) {
        return decoder->ogg_fd;
    }

    callbacks.read_func = ogg_read_cb;
    callbacks.seek_func = ogg_seek_cb;
    callbacks.close_func = ogg_close_cb;
    callbacks.tell_func = ogg_tell_cb;
    if (ov_open_callbacks(&decoder->ogg_fd, &decoder->ogg, NULL, 0, callbacks) < 0) {
        sceIoClose(decoder->ogg_fd);
        decoder->ogg_fd = -1;
        return -1;
    }
    decoder->ogg_open = 1;
    info = ov_info(&decoder->ogg, -1);
    if (!info || info->channels < 1 || info->channels > 2 || info->rate <= 0) {
        return -2;
    }
    decoder->sample_rate = (uint32_t)info->rate;
    decoder->channels = (uint8_t)info->channels;
    return 0;
}

static int open_mp3(eqvita_media_decoder_t *decoder, const char *path)
{
    long rate = 0;
    int channels = 0;
    int encoding = 0;
    int err;

    if (mpg123_init() != MPG123_OK) {
        return -1;
    }
    decoder->mp3 = mpg123_new(NULL, &err);
    if (!decoder->mp3) {
        mpg123_exit();
        return -1;
    }
    mpg123_param(decoder->mp3, MPG123_FLAGS, MPG123_FORCE_SEEKABLE | MPG123_FUZZY | MPG123_GAPLESS, 0.0);
    if (mpg123_open(decoder->mp3, path) != MPG123_OK) {
        return -1;
    }
    if (mpg123_getformat(decoder->mp3, &rate, &channels, &encoding) != MPG123_OK ||
        channels < 1 || channels > 2 || rate <= 0) {
        return -2;
    }
    mpg123_format_none(decoder->mp3);
    mpg123_format(decoder->mp3, rate, channels, MPG123_ENC_SIGNED_16);
    decoder->sample_rate = (uint32_t)rate;
    decoder->channels = (uint8_t)channels;
    (void)encoding;
    return 0;
}

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int read_wav_header(eqvita_media_decoder_t *decoder, const char *path)
{
    unsigned char riff[12];
    int have_fmt = 0;
    int have_data = 0;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sample_rate = 0;

    decoder->wav_fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (decoder->wav_fd < 0) {
        return decoder->wav_fd;
    }
    if (sceIoRead(decoder->wav_fd, riff, sizeof(riff)) != (int)sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(riff + 8, "WAVE", 4) != 0) {
        return -2;
    }

    while (!have_fmt || !have_data) {
        unsigned char chunk[8];
        uint32_t size;
        SceOff data_start;

        if (sceIoRead(decoder->wav_fd, chunk, sizeof(chunk)) != (int)sizeof(chunk)) {
            return -2;
        }
        size = read_u32_le(chunk + 4);
        data_start = sceIoLseek(decoder->wav_fd, 0, SCE_SEEK_CUR);
        if (data_start < 0) {
            return -2;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[32];
            uint32_t read_size = size < sizeof(fmt) ? size : (uint32_t)sizeof(fmt);
            if (read_size < 16 ||
                sceIoRead(decoder->wav_fd, fmt, read_size) != (int)read_size) {
                return -2;
            }
            audio_format = read_u16_le(fmt);
            channels = read_u16_le(fmt + 2);
            sample_rate = read_u32_le(fmt + 4);
            bits = read_u16_le(fmt + 14);
            have_fmt = 1;
            if (size > read_size &&
                sceIoLseek(decoder->wav_fd, (SceOff)(size - read_size), SCE_SEEK_CUR) < 0) {
                return -2;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            decoder->wav_data_start = (uint32_t)data_start;
            decoder->wav_data_bytes = size;
            decoder->wav_bytes_read = 0;
            have_data = 1;
            if (sceIoLseek(decoder->wav_fd, (SceOff)(size + (size & 1u)), SCE_SEEK_CUR) < 0) {
                return -2;
            }
        } else {
            if (sceIoLseek(decoder->wav_fd, (SceOff)(size + (size & 1u)), SCE_SEEK_CUR) < 0) {
                return -2;
            }
        }
    }

    if (audio_format != 1 || bits != 16 || channels < 1 || channels > 2 || sample_rate == 0 ||
        decoder->wav_data_start == 0 || decoder->wav_data_bytes == 0) {
        return -2;
    }
    if (sceIoLseek(decoder->wav_fd, (SceOff)decoder->wav_data_start, SCE_SEEK_SET) < 0) {
        return -2;
    }

    decoder->sample_rate = sample_rate;
    decoder->channels = (uint8_t)channels;
    return 0;
}

static int open_wav(eqvita_media_decoder_t *decoder, const char *path)
{
    return read_wav_header(decoder, path);
}
#endif

static void eqvita_media_player_close_decoder(eqvita_media_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }
#ifndef EQVITA_HOST_TESTS
    if (decoder->ogg_open) {
        ov_clear(&decoder->ogg);
        decoder->ogg_open = 0;
        decoder->ogg_fd = -1;
    } else if (decoder->ogg_fd >= 0) {
        sceIoClose(decoder->ogg_fd);
        decoder->ogg_fd = -1;
    }
    if (decoder->mp3) {
        mpg123_close(decoder->mp3);
        mpg123_delete(decoder->mp3);
        decoder->mp3 = NULL;
        mpg123_exit();
    }
    if (decoder->wav_fd >= 0) {
        sceIoClose(decoder->wav_fd);
        decoder->wav_fd = -1;
    }
#endif
    memset(decoder, 0, sizeof(*decoder));
#ifndef EQVITA_HOST_TESTS
    decoder->ogg_fd = -1;
    decoder->wav_fd = -1;
#endif
}

#ifndef EQVITA_HOST_TESTS
static int decoder_seek_start(eqvita_media_decoder_t *decoder)
{
    if (!decoder) {
        return -1;
    }
    decoder->eof = 0;
    switch (decoder->format) {
        case EQVITA_MEDIA_FORMAT_OGG:
            return ov_pcm_seek(&decoder->ogg, 0);
        case EQVITA_MEDIA_FORMAT_MP3:
            return mpg123_seek(decoder->mp3, 0, SEEK_SET) < 0 ? -1 : 0;
        case EQVITA_MEDIA_FORMAT_WAV:
            decoder->wav_bytes_read = 0;
            return sceIoLseek(decoder->wav_fd, (SceOff)decoder->wav_data_start, SCE_SEEK_SET) < 0 ? -1 : 0;
        default:
            return -1;
    }
}

static int decoder_read(eqvita_media_decoder_t *decoder, int16_t *out, int frames)
{
    int channels;
    int want_bytes;
    int got_bytes = 0;

    if (!decoder || !out || frames <= 0) {
        return -1;
    }

    channels = decoder->channels;
    want_bytes = frames * channels * (int)sizeof(int16_t);
    memset(out, 0, (size_t)want_bytes);

    switch (decoder->format) {
        case EQVITA_MEDIA_FORMAT_OGG:
            while (got_bytes < want_bytes) {
                long ret = ov_read(&decoder->ogg,
                                   ((char *)out) + got_bytes,
                                   want_bytes - got_bytes,
                                   0,
                                   2,
                                   1,
                                   &decoder->ogg_section);
                if (ret < 0) {
                    return -1;
                }
                if (ret == 0) {
                    decoder->eof = 1;
                    break;
                }
                got_bytes += (int)ret;
            }
            break;
        case EQVITA_MEDIA_FORMAT_MP3: {
            size_t done = 0;
            int ret = mpg123_read(decoder->mp3, (unsigned char *)out, (size_t)want_bytes, &done);
            got_bytes = (int)done;
            if (ret == MPG123_DONE) {
                decoder->eof = 1;
            } else if (ret != MPG123_OK && ret != MPG123_NEW_FORMAT) {
                return -1;
            }
            break;
        }
        case EQVITA_MEDIA_FORMAT_WAV: {
            uint32_t left = decoder->wav_data_bytes - decoder->wav_bytes_read;
            uint32_t to_read = left < (uint32_t)want_bytes ? left : (uint32_t)want_bytes;
            int got = to_read > 0 ? sceIoRead(decoder->wav_fd, out, to_read) : 0;
            if (got < 0) {
                return -1;
            }
            got_bytes = got;
            decoder->wav_bytes_read += (uint32_t)got;
            if (got_bytes < want_bytes || decoder->wav_bytes_read >= decoder->wav_data_bytes) {
                decoder->eof = 1;
            }
            break;
        }
        default:
            return -1;
    }

    return got_bytes / (channels * (int)sizeof(int16_t));
}

static void apply_volume(int16_t *samples, int sample_count, int volume)
{
    for (int i = 0; i < sample_count; ++i) {
        int v = (samples[i] * volume) / EQVITA_MEDIA_PLAYER_VOLUME_MAX;
        samples[i] = (int16_t)clamp_int(v, -32768, 32767);
    }
}

static int player_thread(unsigned int args, void *argp)
{
    eqvita_media_player_t *player = NULL;
    eqvita_media_decoder_t *decoder;
    int16_t buffer[PREVIEW_FRAMES * 2];

    if (args == sizeof(player) && argp) {
        memcpy(&player, argp, sizeof(player));
    }

    if (!player || !player->decoder) {
        sceKernelExitThread(-1);
        return -1;
    }

    decoder = (eqvita_media_decoder_t *)player->decoder;
    while (1) {
        int frames;
        int paused = 0;
        int loop = 0;
        int volume = EQVITA_MEDIA_PLAYER_DEFAULT_VOLUME;

        if (eqvita_media_player_command_snapshot(player, &paused, &loop, &volume)) {
            break;
        }

        if (paused) {
            eqvita_media_player_set_state(player, EQVITA_MEDIA_PLAYER_PAUSED);
            sceKernelDelayThread(10000);
            continue;
        }

        eqvita_media_player_set_state(player, EQVITA_MEDIA_PLAYER_PLAYING);
        frames = decoder_read(decoder, buffer, PREVIEW_FRAMES);
        if (frames < 0) {
            eqvita_media_player_set_error(player, frames);
            break;
        }
        if (frames <= 0 || decoder->eof) {
            if (loop && decoder_seek_start(decoder) == 0) {
                continue;
            }
            eqvita_media_player_set_state(player, EQVITA_MEDIA_PLAYER_FINISHED);
            break;
        }

        apply_volume(buffer, frames * decoder->channels, volume);
        if (sceAudioOutOutput(player->audio_port, buffer) < 0) {
            eqvita_media_player_set_error(player, -1);
            break;
        }
    }

    sceKernelExitThread(0);
    return 0;
}
#endif

void eqvita_media_player_init(eqvita_media_player_t *player)
{
    if (!player) {
        return;
    }
    memset(player, 0, sizeof(*player));
    player->status.state = EQVITA_MEDIA_PLAYER_STOPPED;
    player->status.volume = EQVITA_MEDIA_PLAYER_DEFAULT_VOLUME;
    player->thread_id = -1;
    player->audio_port = -1;
#ifndef EQVITA_HOST_TESTS
    if (sceKernelCreateLwMutex(&player->status_mutex, "eqvita_preview_status", 0, 1, NULL) >= 0) {
        player->status_mutex_ready = 1;
    }
#endif
}

void eqvita_media_player_stop(eqvita_media_player_t *player)
{
    if (!player) {
        return;
    }

#ifndef EQVITA_HOST_TESTS
    eqvita_media_player_lock(player);
    player->stop_requested = 1;
    eqvita_media_player_unlock(player);
    if (player->thread_id >= 0) {
        sceKernelWaitThreadEnd(player->thread_id, NULL, NULL);
        player->thread_id = -1;
    }
    if (player->audio_port >= 0) {
        sceAudioOutReleasePort(player->audio_port);
        player->audio_port = -1;
    }
    if (player->bgm_acquired) {
        sceAppMgrReleaseBgmPort();
        player->bgm_acquired = 0;
    }
#endif
    eqvita_media_player_close_decoder((eqvita_media_decoder_t *)player->decoder);
    free(player->decoder);
    player->decoder = NULL;
    eqvita_media_player_lock(player);
    player->stop_requested = 0;
    player->pause_requested = 0;
    if (player->status.state != EQVITA_MEDIA_PLAYER_FINISHED &&
        player->status.state != EQVITA_MEDIA_PLAYER_ERROR) {
        player->status.state = EQVITA_MEDIA_PLAYER_STOPPED;
    }
    eqvita_media_player_unlock(player);
}

void eqvita_media_player_shutdown(eqvita_media_player_t *player)
{
    if (!player) {
        return;
    }
    eqvita_media_player_stop(player);
#ifndef EQVITA_HOST_TESTS
    if (player->status_mutex_ready) {
        sceKernelDeleteLwMutex(&player->status_mutex);
        player->status_mutex_ready = 0;
    }
#endif
}

int eqvita_media_player_open(eqvita_media_player_t *player, const char *path)
{
    eqvita_media_decoder_t *decoder;
    eqvita_media_player_format_t format;
    int ret = -1;

    if (!player || !path || !eqvita_media_browser_is_supported_file(path)) {
        return -1;
    }

    eqvita_media_player_stop(player);

    decoder = (eqvita_media_decoder_t *)calloc(1, sizeof(*decoder));
    if (!decoder) {
        eqvita_media_player_set_error(player, -1);
        return -1;
    }
#ifndef EQVITA_HOST_TESTS
    decoder->ogg_fd = -1;
    decoder->wav_fd = -1;
#endif
    format = format_from_path(path);
    decoder->format = format;

#ifndef EQVITA_HOST_TESTS
    if (format == EQVITA_MEDIA_FORMAT_OGG) {
        ret = open_ogg(decoder, path);
    } else if (format == EQVITA_MEDIA_FORMAT_MP3) {
        ret = open_mp3(decoder, path);
    } else if (format == EQVITA_MEDIA_FORMAT_WAV) {
        ret = open_wav(decoder, path);
    }
#else
    ret = 0;
    decoder->sample_rate = 48000;
    decoder->channels = 2;
#endif

    if (ret < 0 || !sample_rate_supported(decoder->sample_rate) ||
        decoder->channels < 1 || decoder->channels > 2) {
        eqvita_media_player_set_error(player, ret < 0 ? ret : -2);
        eqvita_media_player_close_decoder(decoder);
        free(decoder);
        return -1;
    }

    eqvita_media_player_lock(player);
    player->decoder = decoder;
    player->stop_requested = 0;
    player->pause_requested = 0;
    player->status.state = EQVITA_MEDIA_PLAYER_STOPPED;
    player->status.format = format;
    player->status.sample_rate = decoder->sample_rate;
    player->status.channels = decoder->channels;
    player->status.last_error = 0;
    snprintf(player->status.path, sizeof(player->status.path), "%s", path);
    eqvita_media_player_unlock(player);

#ifndef EQVITA_HOST_TESTS
    ret = sceAppMgrAcquireBgmPort();
    if (ret < 0) {
        eqvita_media_player_set_error(player, ret);
        eqvita_media_player_stop(player);
        return -1;
    }
    player->bgm_acquired = 1;

    player->audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                             PREVIEW_FRAMES,
                                             (int)decoder->sample_rate,
                                             decoder->channels == 2 ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO);
    if (player->audio_port < 0) {
        eqvita_media_player_set_error(player, player->audio_port);
        eqvita_media_player_stop(player);
        return -1;
    }

    player->thread_id = sceKernelCreateThread("eqvita_preview",
                                              player_thread,
                                              PREVIEW_THREAD_PRIORITY,
                                              PREVIEW_THREAD_STACK,
                                              0,
                                              0,
                                              NULL);
    if (player->thread_id < 0) {
        eqvita_media_player_set_error(player, player->thread_id);
        eqvita_media_player_stop(player);
        return -1;
    }
    ret = sceKernelStartThread(player->thread_id, sizeof(player), &player);
    if (ret < 0) {
        eqvita_media_player_set_error(player, ret);
        eqvita_media_player_stop(player);
        return -1;
    }
#else
    eqvita_media_player_set_state(player, EQVITA_MEDIA_PLAYER_PLAYING);
#endif
    return 0;
}

void eqvita_media_player_toggle_pause(eqvita_media_player_t *player)
{
    eqvita_media_player_status_t status = eqvita_media_player_status(player);
    if (!player || !player->decoder ||
        (status.state != EQVITA_MEDIA_PLAYER_PLAYING &&
         status.state != EQVITA_MEDIA_PLAYER_PAUSED)) {
        return;
    }
    eqvita_media_player_lock(player);
    player->pause_requested = !player->pause_requested;
    player->status.state = player->pause_requested ? EQVITA_MEDIA_PLAYER_PAUSED : EQVITA_MEDIA_PLAYER_PLAYING;
    eqvita_media_player_unlock(player);
}

void eqvita_media_player_set_loop(eqvita_media_player_t *player, int loop)
{
    if (!player) {
        return;
    }
    eqvita_media_player_lock(player);
    player->status.loop = loop ? 1 : 0;
    eqvita_media_player_unlock(player);
}

void eqvita_media_player_adjust_volume(eqvita_media_player_t *player, int delta)
{
    if (!player) {
        return;
    }
    eqvita_media_player_lock(player);
    player->status.volume = clamp_int(player->status.volume + delta, 0, EQVITA_MEDIA_PLAYER_VOLUME_MAX);
    eqvita_media_player_unlock(player);
}

eqvita_media_player_status_t eqvita_media_player_status(const eqvita_media_player_t *player)
{
    eqvita_media_player_status_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.state = EQVITA_MEDIA_PLAYER_STOPPED;
    empty.volume = EQVITA_MEDIA_PLAYER_DEFAULT_VOLUME;
    if (!player) {
        return empty;
    }
    eqvita_media_player_lock(player);
    empty = player->status;
    eqvita_media_player_unlock(player);
    return empty;
}

const char *eqvita_media_player_state_label(eqvita_media_player_state_t state)
{
    switch (state) {
        case EQVITA_MEDIA_PLAYER_PLAYING: return "Playing";
        case EQVITA_MEDIA_PLAYER_PAUSED: return "Paused";
        case EQVITA_MEDIA_PLAYER_FINISHED: return "Finished";
        case EQVITA_MEDIA_PLAYER_ERROR: return "Error";
        case EQVITA_MEDIA_PLAYER_STOPPED:
        default: return "Stopped";
    }
}

const char *eqvita_media_player_format_label(eqvita_media_player_format_t format)
{
    switch (format) {
        case EQVITA_MEDIA_FORMAT_OGG: return "OGG";
        case EQVITA_MEDIA_FORMAT_MP3: return "MP3";
        case EQVITA_MEDIA_FORMAT_WAV: return "WAV";
        case EQVITA_MEDIA_FORMAT_NONE:
        default: return "-";
    }
}
