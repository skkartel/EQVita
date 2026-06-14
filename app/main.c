#include <psp2/ctrl.h>
#include <psp2/avconfig.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ui_vita.h"
#include "../common/eq_shared.h"

#define STEP_FINE 500
#define STEP_COARSE 1000
#define PRESET_SYNC_FAILED -4
#define PRESET_PATH_FMT "ur0:data/eqvita/preset%d.eqvp"
#define LEGACY_PRESET_PATH_FMT "ur0:data/eqvita/preset%d.bin"
#define BOOT_STATE_PATH "ur0:data/eqvita/boot.eqbs"
#define THEME_PATH "ur0:data/eqvita/theme.cfg"
#define APP_LOG_PATH "ur0:data/eqvita/app.log"
#define STATUS_LOG_INTERVAL_FRAMES 60
#define THEME_FILE_MAGIC 0x4d545145u
#define THEME_FILE_VERSION 1u

#define SCE_AVCONFIG_VOLCTRL_ONBOARD 1
#define SCE_AVCONFIG_VOLCTRL_BLUETOOTH 2
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_0 0x001
#define SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT 0x004
#define SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT 0x010
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_8 0x100

int sceAVConfigGetConnectedAudioDevice(uint32_t *flags);
int sceAVConfigGetVolCtrlEnable(uint32_t *volCtrl, int *muted, int *avls);

typedef enum app_screen
{
    SCREEN_HOME = 0,
    SCREEN_STATUS,
    SCREEN_PRESETS,
    SCREEN_SIMPLE,
    SCREEN_ADVANCED,
    SCREEN_THEMES,
    SCREEN_SETTINGS,
    SCREEN_ABOUT,
    SCREEN_COUNT
} app_screen_t;

static const char *band_labels[EQ_BANDS] = {
    "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"
};

#define STATUS_ROW_EQ_STATUS 0
#define STATUS_ROW_OUTPUT 1
#define STATUS_ROW_PRESET 2
#define STATUS_ROW_PLUGIN 3
#define STATUS_ROW_LEVELS_SECTION 4
#define STATUS_ROW_CLIPPING 5
#define STATUS_ROW_PEAK 6
#define STATUS_ROW_AUDIO_RATE 7
#define STATUS_ROW_DIAGNOSTICS_SECTION 8
#define STATUS_ROW_STREAMS 9
#define STATUS_ROW_SKIPPED 10
#define STATUS_ROW_UNKNOWN 11
#define STATUS_ROW_LAST_BLOCK 12
#define STATUS_ROW_SLOWEST_BLOCK 13
#define STATUS_ROW_BLOCKS 14
#define STATUS_ROW_COUNT 15

#define PRESETS_ROW_SLOT 0
#define PRESETS_ROW_STOCK_DEPTH 1
#define PRESETS_ROW_MOD_SWITCH 2
#define PRESETS_ROW_ACTIONS_SECTION 3
#define PRESETS_ROW_SAVE 4
#define PRESETS_ROW_LOAD 5
#define PRESETS_ROW_RESET 6
#define PRESETS_ROW_COUNT 7

#define SIMPLE_ROW_PREAMP 0
#define SIMPLE_ROW_BASS 1
#define SIMPLE_ROW_MIDRANGE 2
#define SIMPLE_ROW_TREBLE 3
#define SIMPLE_ROW_PRESET_SECTION 4
#define SIMPLE_ROW_PRESET_SLOT 5
#define SIMPLE_ROW_SAVE_CURRENT 6
#define SIMPLE_ROW_SAVE_NEXT 7
#define SIMPLE_ROW_UNDO_ENTRY 8
#define SIMPLE_ROW_RESET_EQ 9
#define SIMPLE_ROW_COUNT 10

#define ADV_BAND_ROW_BASE 0
#define ADV_ROW_PRESET_SECTION (ADV_BAND_ROW_BASE + EQ_BANDS)
#define ADV_ROW_PRESET_SLOT (ADV_ROW_PRESET_SECTION + 1)
#define ADV_ROW_SAVE_CURRENT (ADV_ROW_PRESET_SECTION + 2)
#define ADV_ROW_SAVE_NEXT (ADV_ROW_PRESET_SECTION + 3)
#define ADV_ROW_UNDO_ENTRY (ADV_ROW_PRESET_SECTION + 4)
#define ADV_ROW_RESET_EQ (ADV_ROW_PRESET_SECTION + 5)
#define ADVANCED_ROW_COUNT (ADV_ROW_PRESET_SECTION + 6)

#define SETTINGS_ROW_ENABLED 0
#define SETTINGS_ROW_SCOPE 1
#define SETTINGS_ROW_HPF 2
#define SETTINGS_ROW_SAFETY_SECTION 3
#define SETTINGS_ROW_HEADROOM 4
#define SETTINGS_ROW_ROUTE 5
#define SETTINGS_ROW_STARTUP 6
#define SETTINGS_ROW_COUNT 7

#define ABOUT_ROW_CREATOR 0
#define ABOUT_ROW_REPOSITORY 1
#define ABOUT_ROW_LOG_FILE 2
#define ABOUT_ROW_WHAT 3
#define ABOUT_ROW_VERSION 4
#define ABOUT_ROW_PLUGIN 5
#define ABOUT_ROW_EQ_SECTION 6
#define ABOUT_ROW_PRESETS 7
#define ABOUT_ROW_SIMPLE 8
#define ABOUT_ROW_ADVANCED 9
#define ABOUT_ROW_PREAMP 10
#define ABOUT_ROW_HPF 11
#define ABOUT_ROW_OUTPUT_SECTION 12
#define ABOUT_ROW_SPEAKERS 13
#define ABOUT_ROW_ALL_OUTPUTS 14
#define ABOUT_ROW_BYPASS 15
#define ABOUT_ROW_DISTORTION 16
#define ABOUT_ROW_CONTROLS_SECTION 17
#define ABOUT_ROW_CONTROLS 18
#define ABOUT_ROW_DATA_FOLDER 19
#define ABOUT_ROW_COUNT 20

static eq_control_t g_control;
static eq_status_t g_status;
static eq_version_t g_version;
static int g_preset_slot = 0;
static int g_plugin_compatible = 0;
static char g_message[96];
static int g_message_frames = 0;
static uint32_t g_status_log_frames = 0;

static app_screen_t g_screen = SCREEN_HOME;
static int g_selected[SCREEN_COUNT];
static int g_scroll_top[SCREEN_COUNT];
static eq_control_t g_eq_entry_control;
static int g_eq_entry_valid = 0;
static eq_ui_row_bounds_t g_row_bounds[EQ_UI_MAX_VISIBLE_ROWS];
static int g_row_bound_count = 0;

static SceTouchPanelInfo g_touch_panel;
static int g_touch_panel_ready = 0;
static int g_touch_down = 0;
static int g_touch_start_x = 0;
static int g_touch_start_y = 0;
static int g_touch_last_y = 0;
static int g_touch_moved = 0;

static int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ensure_data_dir(void)
{
    sceIoMkdir("ur0:data", 0777);
    sceIoMkdir("ur0:data/eqvita", 0777);
}

typedef struct app_theme_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t theme_index;
    uint32_t checksum;
} app_theme_file_t;

static uint32_t theme_file_checksum(uint32_t theme_index)
{
    return THEME_FILE_MAGIC ^ THEME_FILE_VERSION ^ theme_index ^ 0x45515448u;
}

static int load_theme_index(void)
{
    app_theme_file_t file;
    SceUID fd = sceIoOpen(THEME_PATH, SCE_O_RDONLY, 0);
    int read_res;

    if (fd < 0) {
        return EQ_UI_DEFAULT_THEME_INDEX;
    }

    read_res = sceIoRead(fd, &file, sizeof(file));
    sceIoClose(fd);

    if (read_res != (int)sizeof(file) ||
        file.magic != THEME_FILE_MAGIC ||
        file.version != THEME_FILE_VERSION ||
        file.checksum != theme_file_checksum(file.theme_index) ||
        file.theme_index >= (uint32_t)eq_ui_theme_count()) {
        return EQ_UI_DEFAULT_THEME_INDEX;
    }

    return (int)file.theme_index;
}

static int save_theme_index(int index)
{
    app_theme_file_t file;
    SceUID fd;
    int written;
    int close_res;

    if (index < 0 || index >= eq_ui_theme_count()) {
        index = EQ_UI_DEFAULT_THEME_INDEX;
    }

    ensure_data_dir();
    file.magic = THEME_FILE_MAGIC;
    file.version = THEME_FILE_VERSION;
    file.theme_index = (uint32_t)index;
    file.checksum = theme_file_checksum(file.theme_index);

    fd = sceIoOpen(THEME_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        return fd;
    }

    written = sceIoWrite(fd, &file, sizeof(file));
    close_res = sceIoClose(fd);
    if (written != (int)sizeof(file)) {
        return written < 0 ? written : -1;
    }
    return close_res;
}

static void app_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int len;

    ensure_data_dir();

    va_start(ap, fmt);
    len = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return;
    }
    if (len >= (int)sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    SceUID fd = sceIoOpen(APP_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0) {
        return;
    }
    sceIoWrite(fd, line, len);
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}

static const char *route_str(uint8_t r)
{
    switch (r) {
        case EQ_ROUTE_SPEAKER: return "Speakers";
        case EQ_ROUTE_HEADPHONES: return "Wired";
        case EQ_ROUTE_BLUETOOTH: return "Bluetooth";
        default: return "Unknown";
    }
}

static const char *bypass_reason_str(uint8_t r)
{
    switch (r) {
        case EQ_BYPASS_NONE: return "Active";
        case EQ_BYPASS_DISABLED: return "Disabled";
        case EQ_BYPASS_SPEAKER_ONLY: return "Waiting for speakers";
        case EQ_BYPASS_UNKNOWN_ROUTE: return "Unknown output";
        case EQ_BYPASS_INVALID_PORT: return "Invalid port";
        case EQ_BYPASS_BUFFER_TOO_LARGE: return "Large buffer";
        case EQ_BYPASS_COPY_FAILED: return "Copy failed";
        case EQ_BYPASS_UNSUPPORTED_FORMAT: return "Unsupported";
        case EQ_BYPASS_AUDIO_BUSY: return "Audio busy";
        default: return "Bypassed";
    }
}

static const char *headroom_mode_str(uint8_t mode)
{
    switch (mode) {
        case EQ_HEADROOM_LOUD: return "Loud";
        case EQ_HEADROOM_RAW: return "Direct";
        case EQ_HEADROOM_SAFE:
        default: return "Safe";
    }
}

static const char *eq_target_str(void)
{
    return g_control.speaker_only ? "Speakers" : "All outputs";
}

static int save_preset(void);
static int save_boot_state(void);
static void persist_current_settings_quietly(void);

static void set_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_message, sizeof(g_message), fmt, ap);
    va_end(ap);
    g_message_frames = 180;
    app_log("message: %s", g_message);
}

static void apply_theme_index(int index)
{
    int set_res;
    int save_res;

    if (index < 0 || index >= eq_ui_theme_count()) {
        index = EQ_UI_DEFAULT_THEME_INDEX;
    }

    set_res = eq_ui_set_theme(index);
    save_res = save_theme_index(index);
    if (set_res < 0) {
        set_message("Theme changed, icon sheet missing");
    } else if (save_res < 0) {
        set_message("Theme changed, save failed (%d)", save_res);
    } else {
        set_message("Theme: %s", eq_ui_theme_name(index));
    }
}

static eq_route_t detect_route_user(void)
{
    uint32_t flags = 0;
    uint32_t vol_ctrl = 0;
    int muted = 0;
    int avls = 0;

    if (sceAVConfigGetConnectedAudioDevice(&flags) >= 0) {
        int have_vol = (sceAVConfigGetVolCtrlEnable(&vol_ctrl, &muted, &avls) >= 0);
        (void)muted;
        (void)avls;

        if (flags & SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT) {
            if (!have_vol || vol_ctrl == SCE_AVCONFIG_VOLCTRL_BLUETOOTH) {
                return EQ_ROUTE_BLUETOOTH;
            }
        }

        if (flags & SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT) {
            if (!have_vol || vol_ctrl == SCE_AVCONFIG_VOLCTRL_ONBOARD) {
                return EQ_ROUTE_HEADPHONES;
            }
        }

        if ((flags & (SCE_AVCONFIG_AUDIO_DEVICE_VITA_0 | SCE_AVCONFIG_AUDIO_DEVICE_VITA_8)) &&
            !(flags & (SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT | SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT))) {
            return EQ_ROUTE_SPEAKER;
        }
    }

    SceCtrlData data;
    memset(&data, 0, sizeof(data));
    if (sceCtrlPeekBufferPositive(0, &data, 1) >= 0 && (data.buttons & SCE_CTRL_HEADPHONE)) {
        return EQ_ROUTE_HEADPHONES;
    }

    return EQ_ROUTE_UNKNOWN;
}

static void maybe_log_status(void)
{
    static uint32_t last_counter = 0;
    static uint8_t last_route = 0xffu;
    static uint8_t last_reason = 0xffu;
    static uint8_t last_active = 0xffu;
    int changed;
    int force_log;

    if (!g_plugin_compatible) {
        return;
    }

    g_status_log_frames++;
    force_log = (g_status_log_frames >= STATUS_LOG_INTERVAL_FRAMES);
    changed = (g_status.status_counter != last_counter ||
        g_status.route != last_route ||
        g_status.bypass_reason != last_reason ||
        g_status.eq_active != last_active);

    if (g_status_log_frames < STATUS_LOG_INTERVAL_FRAMES && !changed) {
        return;
    }

    if (force_log || changed) {
        g_status_log_frames = 0;
        app_log("status: route=%s active=%u reason=%s sr=%u port=%u len=%u ch=%u runs=%u ports=%u busy=%u unknown=%u last_us=%u max_us=%u clips=%d peak_l=%u peak_r=%u",
            route_str(g_status.route),
            g_status.eq_active,
            bypass_reason_str(g_status.bypass_reason),
            g_status.sample_rate,
            g_status.debug_port,
            g_status.debug_len,
            g_status.debug_channels,
            g_status.debug_run_count,
            g_status.debug_active_ports,
            g_status.debug_busy_bypass_count,
            g_status.debug_unknown_port_count,
            g_status.debug_last_us,
            g_status.debug_max_us,
            g_status.clip_events,
            g_status.peak_l,
            g_status.peak_r);

        last_counter = g_status.status_counter;
        last_route = g_status.route;
        last_reason = g_status.bypass_reason;
        last_active = g_status.eq_active;
    }
}

static int mark_dirty(void)
{
    int set_res;
    int status_res;

    if (!g_plugin_compatible) {
        return -1;
    }

    g_control.route_hint = (uint8_t)detect_route_user();
    g_control.dirty_counter++;
    set_res = EqSetControl(&g_control);
    status_res = (set_res >= 0) ? EqGetStatus(&g_status) : -1;
    if (set_res < 0 || status_res < 0) {
        set_message("Plugin communication failed (%d/%d)", set_res, status_res);
    }
    return set_res;
}

static void refresh_route_hint(void)
{
    uint8_t route;
    if (!g_plugin_compatible) {
        return;
    }

    route = (uint8_t)detect_route_user();
    if (g_control.route_hint != route) {
        g_control.route_hint = route;
        g_control.dirty_counter++;
        if (EqSetControl(&g_control) < 0) {
            set_message("Route update failed");
        }
    }
}

static void toggle_enabled(void)
{
    g_control.enabled = !g_control.enabled;
    mark_dirty();
}

static void toggle_speaker_only(void)
{
    g_control.speaker_only = !g_control.speaker_only;
    if (mark_dirty() == 0) {
        persist_current_settings_quietly();
    }
}

static void toggle_hpf(void)
{
    eq_control_set_hpf_enabled(&g_control, !eq_control_hpf_enabled(&g_control));
    mark_dirty();
}

static void adjust_headroom_mode(int delta)
{
    int mode = (int)eq_control_get_headroom_mode(&g_control) + delta;
    if (mode < 0) mode = EQ_HEADROOM_RAW;
    if (mode > EQ_HEADROOM_RAW) mode = EQ_HEADROOM_SAFE;
    eq_control_set_headroom_mode(&g_control, (uint8_t)mode);
    mark_dirty();
}

static void adjust_preamp(int delta)
{
    int v = g_control.preamp_mdB + delta;
    g_control.preamp_mdB = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static void adjust_band(int idx, int delta)
{
    int v = g_control.band_gain_mdB[idx] + delta;
    g_control.band_gain_mdB[idx] = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static int save_preset(void)
{
    char path[64];
    SceUID fd;
    eq_preset_file_t preset;
    int written;
    int close_res;

    ensure_data_dir();
    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    eq_preset_build(&preset, &g_control);
    written = sceIoWrite(fd, &preset, sizeof(preset));
    close_res = sceIoClose(fd);
    if (written != (int)sizeof(preset)) {
        return written < 0 ? written : -1;
    }
    return close_res;
}

static int save_boot_state(void)
{
    eq_control_t boot_control = g_control;
    SceUID fd;
    eq_boot_state_file_t state;
    int written;
    int close_res;

    ensure_data_dir();
    boot_control.route_hint = (uint8_t)detect_route_user();
    fd = sceIoOpen(BOOT_STATE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    eq_boot_state_build(&state, &boot_control);
    written = sceIoWrite(fd, &state, sizeof(state));
    close_res = sceIoClose(fd);
    if (written != (int)sizeof(state)) {
        return written < 0 ? written : -1;
    }
    return close_res;
}

static void persist_current_settings_quietly(void)
{
    int preset_res = save_preset();
    int boot_res = preset_res >= 0 ? save_boot_state() : preset_res;
    if (preset_res < 0 || boot_res < 0) {
        set_message("Save failed (%d/%d)", preset_res, boot_res);
    }
}

static int load_preset(void)
{
    char path[64];
    eq_control_t previous = g_control;
    eq_preset_primary_status_t primary_status = EQ_PRESET_PRIMARY_MISSING;
    SceUID fd;

    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_preset_file_t preset;
        eq_control_t loaded;
        int r = sceIoRead(fd, &preset, sizeof(preset));
        sceIoClose(fd);
        if (r == sizeof(preset) && eq_preset_extract_control(&preset, &loaded) == 0) {
            primary_status = EQ_PRESET_PRIMARY_VALID;
            g_control = loaded;
            if (mark_dirty() < 0) {
                g_control = previous;
                return PRESET_SYNC_FAILED;
            }
            return 0;
        }
        primary_status = EQ_PRESET_PRIMARY_INVALID;
    }

    if (!eq_preset_should_try_legacy(primary_status)) {
        return -2;
    }

    snprintf(path, sizeof(path), LEGACY_PRESET_PATH_FMT, g_preset_slot);
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_control_t tmp;
        int r = sceIoRead(fd, &tmp, sizeof(tmp));
        sceIoClose(fd);
        if (r == sizeof(tmp) && eq_control_validate(&tmp) == 0) {
            g_control = tmp;
            if (mark_dirty() < 0) {
                g_control = previous;
                return PRESET_SYNC_FAILED;
            }
            return 1;
        }
        return -3;
    }

    return fd;
}

static void save_preset_with_message(void)
{
    int res = save_preset();
    if (res >= 0) {
        int boot_res = save_boot_state();
        if (boot_res >= 0) {
            set_message("Saved preset %d", g_preset_slot + 1);
        } else {
            set_message("Preset saved, boot state failed");
        }
    } else {
        set_message("Save failed (%d)", res);
    }
}

static void save_as_next_preset(void)
{
    g_preset_slot = (g_preset_slot + 1) % 3;
    save_preset_with_message();
}

static void load_preset_with_message(void)
{
    int res = load_preset();
    if (res > 0) {
        set_message("Imported legacy preset %d", g_preset_slot + 1);
    } else if (res == 0) {
        set_message("Loaded preset %d", g_preset_slot + 1);
    } else if (res != PRESET_SYNC_FAILED) {
        set_message("Load failed (%d)", res);
    }
}

static void restore_eq_entry(void)
{
    if (!g_eq_entry_valid) {
        set_message("No edits to undo");
        return;
    }
    g_control = g_eq_entry_control;
    if (mark_dirty() == 0) {
        set_message("Restored screen entry settings");
    }
}

static void reset_defaults(void)
{
    g_control.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        g_control.band_gain_mdB[i] = 0;
    }
    mark_dirty();
}

static void apply_simple_eq(int bass, int mid, int treble, int auto_preamp)
{
    bass = clamp(bass, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mid = clamp(mid, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    treble = clamp(treble, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);

    g_control.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) g_control.band_gain_mdB[i] = bass;
    for (int i = 4; i <= 6; ++i) g_control.band_gain_mdB[i] = mid;
    for (int i = 7; i <= 9; ++i) g_control.band_gain_mdB[i] = treble;

    if (auto_preamp) {
        int32_t max_boost = bass;
        if (mid > max_boost) max_boost = mid;
        if (treble > max_boost) max_boost = treble;
        if (max_boost < 0) max_boost = 0;
        g_control.preamp_mdB = -max_boost;
        if (g_control.preamp_mdB < -EQ_MAX_ABS_GAIN_MDB) g_control.preamp_mdB = -EQ_MAX_ABS_GAIN_MDB;
    }
    mark_dirty();
}

static void apply_preset_stock_depth(void)
{
    g_control.preamp_mdB = -4000;
    g_control.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) g_control.band_gain_mdB[i] = 4000;
    for (int i = 4; i <= 6; ++i) g_control.band_gain_mdB[i] = -2000;
    for (int i = 7; i <= 9; ++i) g_control.band_gain_mdB[i] = 2000;
    if (mark_dirty() == 0) {
        set_message("Applied STOCK Depth");
    }
}

static void apply_preset_mod_switch(void)
{
    static const int32_t gains[EQ_BANDS] = {
        3000, 4000, 4500, 4500, -5000,
        -5000, -5000, -2500, -2500, -3000
    };

    g_control.enabled = 1;
    g_control.speaker_only = 1;
    eq_control_set_hpf_enabled(&g_control, 0);
    eq_control_set_headroom_mode(&g_control, EQ_HEADROOM_LOUD);
    g_control.preamp_mdB = -6500;
    for (int i = 0; i < EQ_BANDS; ++i) {
        g_control.band_gain_mdB[i] = gains[i];
    }
    if (mark_dirty() == 0) {
        set_message("Applied MOD Switch");
    }
}

static void format_db(char *out, size_t out_size, int32_t mdB)
{
    snprintf(out, out_size, "%+0.1f dB", mdB / 1000.0f);
}

static const char *screen_title(app_screen_t screen)
{
    switch (screen) {
        case SCREEN_STATUS: return "Telemetry";
        case SCREEN_PRESETS: return "Presets";
        case SCREEN_SIMPLE: return "Simple EQ";
        case SCREEN_ADVANCED: return "Advanced EQ";
        case SCREEN_THEMES: return "Themes";
        case SCREEN_SETTINGS: return "Settings";
        case SCREEN_ABOUT: return "Help";
        case SCREEN_HOME:
        default: return "EQVita";
    }
}

static const char *screen_subtitle(app_screen_t screen)
{
    switch (screen) {
        case SCREEN_STATUS: return "Live output and app status";
        case SCREEN_PRESETS: return "Choose or save sound profiles";
        case SCREEN_SIMPLE: return "Adjust bass, mids, treble";
        case SCREEN_ADVANCED: return "Fine tune 10 bands";
        case SCREEN_THEMES: return "Choose the app color style";
        case SCREEN_SETTINGS: return "Choose where EQ applies";
        case SCREEN_ABOUT: return "Controls, version, and help";
        case SCREEN_HOME:
        default: return "Output - EQ status - preset slot";
    }
}

static app_screen_t home_screen_for_row(int row)
{
    switch (row) {
        case 1: return SCREEN_SIMPLE;
        case 2: return SCREEN_ADVANCED;
        case 3: return SCREEN_PRESETS;
        case 4: return SCREEN_THEMES;
        case 5: return SCREEN_SETTINGS;
        case 6: return SCREEN_STATUS;
        case 7: return SCREEN_ABOUT;
        default: return SCREEN_HOME;
    }
}

static int current_row_count(void)
{
    switch (g_screen) {
        case SCREEN_HOME: return 8;
        case SCREEN_STATUS: return STATUS_ROW_COUNT;
        case SCREEN_PRESETS: return PRESETS_ROW_COUNT;
        case SCREEN_SIMPLE: return SIMPLE_ROW_COUNT;
        case SCREEN_ADVANCED: return ADVANCED_ROW_COUNT;
        case SCREEN_THEMES: return eq_ui_theme_count();
        case SCREEN_SETTINGS: return SETTINGS_ROW_COUNT;
        case SCREEN_ABOUT: return ABOUT_ROW_COUNT;
        default: return 0;
    }
}

static int row_is_section(app_screen_t screen, int row)
{
    switch (screen) {
        case SCREEN_STATUS:
            return row == STATUS_ROW_LEVELS_SECTION ||
                   row == STATUS_ROW_DIAGNOSTICS_SECTION;
        case SCREEN_PRESETS:
            return row == PRESETS_ROW_ACTIONS_SECTION;
        case SCREEN_SIMPLE:
            return row == SIMPLE_ROW_PRESET_SECTION;
        case SCREEN_ADVANCED:
            return row == ADV_ROW_PRESET_SECTION;
        case SCREEN_SETTINGS:
            return row == SETTINGS_ROW_SAFETY_SECTION;
        case SCREEN_ABOUT:
            return row == ABOUT_ROW_EQ_SECTION ||
                   row == ABOUT_ROW_OUTPUT_SECTION ||
                   row == ABOUT_ROW_CONTROLS_SECTION;
        default:
            return 0;
    }
}

static int row_is_selectable(app_screen_t screen, int row)
{
    return !row_is_section(screen, row);
}

static int visible_row_capacity(void)
{
    return (EQ_UI_FOOTER_Y - EQ_UI_LIST_Y - 8) / (EQ_UI_ROW_H + EQ_UI_ROW_GAP);
}

static void ensure_selection_visible(void)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int *selected = &g_selected[g_screen];
    int *scroll = &g_scroll_top[g_screen];

    if (count <= 0) {
        *selected = 0;
        *scroll = 0;
        return;
    }

    if (*selected < 0) *selected = 0;
    if (*selected >= count) *selected = count - 1;
    if (!row_is_selectable(g_screen, *selected)) {
        if (*selected + 1 < count) {
            (*selected)++;
        } else if (*selected > 0) {
            (*selected)--;
        }
    }
    if (*scroll < 0) *scroll = 0;
    if (*scroll > count - visible) *scroll = count > visible ? count - visible : 0;
    if (*selected < *scroll) *scroll = *selected;
    if (*selected >= *scroll + visible) *scroll = *selected - visible + 1;
}

static void change_screen(app_screen_t screen)
{
    if (screen >= SCREEN_COUNT) {
        return;
    }
    if ((screen == SCREEN_SIMPLE || screen == SCREEN_ADVANCED) && g_screen != screen) {
        g_eq_entry_control = g_control;
        g_eq_entry_valid = 1;
    }
    if (screen == SCREEN_THEMES) {
        g_selected[screen] = eq_ui_theme_index();
    }
    g_screen = screen;
    ensure_selection_visible();
}

static void move_selection(int delta)
{
    int count = current_row_count();
    int *selected = &g_selected[g_screen];
    int next;
    if (count <= 0) {
        return;
    }
    next = *selected;
    for (int i = 0; i < count; ++i) {
        next = (next + delta + count) % count;
        if (row_is_selectable(g_screen, next)) {
            *selected = next;
            break;
        }
    }
    ensure_selection_visible();
}

static void scroll_current(int delta)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int max_scroll = count > visible ? count - visible : 0;
    g_scroll_top[g_screen] = clamp(g_scroll_top[g_screen] + delta, 0, max_scroll);
    g_selected[g_screen] = clamp(g_selected[g_screen], g_scroll_top[g_screen],
                                 g_scroll_top[g_screen] + visible - 1);
    ensure_selection_visible();
}

static void row_value(char *value, size_t value_size, app_screen_t screen, int row)
{
    value[0] = 0;
    switch (screen) {
        case SCREEN_HOME:
            if (row == 0) snprintf(value, value_size, "%s", g_control.enabled ? "On" : "Off");
            else if (row == 3) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            else if (row == 4) snprintf(value, value_size, "%s", eq_ui_theme_name(eq_ui_theme_index()));
            break;
        case SCREEN_STATUS:
            if (row == STATUS_ROW_EQ_STATUS) snprintf(value, value_size, "%s", g_control.enabled ? (g_status.eq_active ? "Active" : bypass_reason_str(g_status.bypass_reason)) : "Off");
            else if (row == STATUS_ROW_OUTPUT) snprintf(value, value_size, "%s", route_str(g_status.route));
            else if (row == STATUS_ROW_PRESET) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            else if (row == STATUS_ROW_PLUGIN) snprintf(value, value_size, "%s", g_plugin_compatible ? "Ready" : "Mismatch");
            else if (row == STATUS_ROW_CLIPPING) snprintf(value, value_size, "%d", g_status.clip_events);
            else if (row == STATUS_ROW_PEAK) snprintf(value, value_size, "%u / %u", g_status.peak_l, g_status.peak_r);
            else if (row == STATUS_ROW_AUDIO_RATE) snprintf(value, value_size, "%u Hz", g_status.sample_rate);
            else if (row == STATUS_ROW_STREAMS) snprintf(value, value_size, "%u", g_status.debug_active_ports);
            else if (row == STATUS_ROW_SKIPPED) snprintf(value, value_size, "%u", g_status.debug_busy_bypass_count);
            else if (row == STATUS_ROW_UNKNOWN) snprintf(value, value_size, "%u", g_status.debug_unknown_port_count);
            else if (row == STATUS_ROW_LAST_BLOCK) snprintf(value, value_size, "%u us", g_status.debug_last_us);
            else if (row == STATUS_ROW_SLOWEST_BLOCK) snprintf(value, value_size, "%u us", g_status.debug_max_us);
            else if (row == STATUS_ROW_BLOCKS) snprintf(value, value_size, "%u", g_status.debug_run_count);
            break;
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_SLOT) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) format_db(value, value_size, g_control.preamp_mdB);
            else if (row == SIMPLE_ROW_BASS) format_db(value, value_size, g_control.band_gain_mdB[1]);
            else if (row == SIMPLE_ROW_MIDRANGE) format_db(value, value_size, g_control.band_gain_mdB[4]);
            else if (row == SIMPLE_ROW_TREBLE) format_db(value, value_size, g_control.band_gain_mdB[7]);
            else if (row == SIMPLE_ROW_PRESET_SLOT) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            else if (row == SIMPLE_ROW_SAVE_NEXT) snprintf(value, value_size, "Slot %d", ((g_preset_slot + 1) % 3) + 1);
            break;
        case SCREEN_ADVANCED:
            if (row >= ADV_BAND_ROW_BASE && row < ADV_BAND_ROW_BASE + EQ_BANDS) {
                format_db(value, value_size, g_control.band_gain_mdB[row - ADV_BAND_ROW_BASE]);
            } else if (row == ADV_ROW_PRESET_SLOT) {
                snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            } else if (row == ADV_ROW_SAVE_NEXT) {
                snprintf(value, value_size, "Slot %d", ((g_preset_slot + 1) % 3) + 1);
            }
            break;
        case SCREEN_THEMES:
            if (row == eq_ui_theme_index()) snprintf(value, value_size, "Active");
            break;
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_ENABLED) snprintf(value, value_size, "%s", g_control.enabled ? "On" : "Off");
            else if (row == SETTINGS_ROW_SCOPE) snprintf(value, value_size, "%s", eq_target_str());
            else if (row == SETTINGS_ROW_HPF) snprintf(value, value_size, "%s", eq_control_hpf_enabled(&g_control) ? "On" : "Off");
            else if (row == SETTINGS_ROW_HEADROOM) snprintf(value, value_size, "%s", headroom_mode_str(eq_control_get_headroom_mode(&g_control)));
            else if (row == SETTINGS_ROW_ROUTE) snprintf(value, value_size, "%s", route_str(g_control.route_hint));
            else if (row == SETTINGS_ROW_STARTUP) snprintf(value, value_size, "Saved");
            break;
        case SCREEN_ABOUT:
            if (row == ABOUT_ROW_VERSION) snprintf(value, value_size, "v%d.%d.%d", g_version.major, g_version.minor, g_version.patch);
            else if (row == ABOUT_ROW_PLUGIN) snprintf(value, value_size, "%s", g_plugin_compatible ? "OK" : "Mismatch");
            else if (row == ABOUT_ROW_LOG_FILE) snprintf(value, value_size, "Share for issues");
            break;
        default:
            break;
    }
}

static void row_text(app_screen_t screen,
                     int row,
                     const char **icon,
                     const char **label,
                     const char **desc,
                     eq_ui_row_kind_t *kind)
{
    *icon = "";
    *label = "";
    *desc = "";
    *kind = EQ_UI_ROW_READONLY;

    switch (screen) {
        case SCREEN_HOME: {
            static const char *icons[] = {"power", "simple", "advanced", "preset", "themes", "settings", "status", "about"};
            static const char *labels[] = {"Equalizer", "Simple EQ", "Advanced EQ", "Presets", "Themes", "Settings", "Telemetry", "Help"};
            static const char *descs[] = {
                "Turn sound tuning on or off",
                "Adjust bass, mids, treble",
                "Fine tune every band",
                "Choose or save profiles",
                "Choose the app color style",
                "Output scope and safety",
                "Live output and app status",
                "Controls and short guide"
            };
            *icon = icons[row]; *label = labels[row]; *desc = descs[row];
            *kind = row == 0 ? EQ_UI_ROW_ACTION : EQ_UI_ROW_NAV;
            break;
        }
        case SCREEN_STATUS:
            if (row_is_section(screen, row)) {
                *label = row == STATUS_ROW_LEVELS_SECTION ? "Audio levels" : "Diagnostics";
                *desc = row == STATUS_ROW_DIAGNOSTICS_SECTION ? "Read-only processing counters" : "";
                *kind = EQ_UI_ROW_SECTION;
            } else {
                *icon = (const char *[]){"status", "speaker", "preset", "status", "level", "level", "info", "level", "status", "info", "info", "info"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 : 11];
                *label = (const char *[]){"EQ status", "Output", "Preset", "Plugin", "Clipping", "Peak level", "Audio rate", "Audio streams", "Skipped audio", "Unknown audio", "Last block", "Slowest block", "Blocks processed"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 :
                    row == STATUS_ROW_SLOWEST_BLOCK ? 11 : 12];
                *desc = (const char *[]){"Active or why it is skipped", "Speakers, wired, or Bluetooth", "Selected profile slot", "App and plugin must match", "Limiter protected the sound", "Recent left and right volume", "Current stream rate", "Streams being watched", "Skipped to avoid stutter", "Streams EQ could not identify", "Latest audio block time", "Slowest audio block time", "Total blocks seen"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 :
                    row == STATUS_ROW_SLOWEST_BLOCK ? 11 : 12];
            }
            break;
        case SCREEN_PRESETS:
            if (row_is_section(screen, row)) {
                *label = "Preset actions";
                *desc = "Save, load, or clear curves";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == PRESETS_ROW_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right changes slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == PRESETS_ROW_STOCK_DEPTH) {
                *icon = "preset";
                *label = "Preset: STOCK Depth";
                *desc = "Warmer stock-speaker profile";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_MOD_SWITCH) {
                *icon = "preset";
                *label = "Preset: MOD Switch";
                *desc = "For Switch speaker mod";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_SAVE) {
                *icon = "save";
                *label = "Save current preset";
                *desc = "Saves preset and startup";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_LOAD) {
                *icon = "load";
                *label = "Load preset";
                *desc = "Loads the selected slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_RESET) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Clears all EQ gains";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) {
                *icon = "level";
                *label = "Preamp";
                *desc = "Overall volume before EQ";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_BASS) {
                *icon = "simple";
                *label = "Bass";
                *desc = "Low sound and depth";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_MIDRANGE) {
                *icon = "simple";
                *label = "Midrange";
                *desc = "Voice and body";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_TREBLE) {
                *icon = "simple";
                *label = "Treble";
                *desc = "Clarity and sparkle";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_PRESET_SECTION) {
                *label = "Preset controls";
                *desc = "Save or undo this curve";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == SIMPLE_ROW_PRESET_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right chooses save slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_SAVE_CURRENT) {
                *icon = "save";
                *label = "Save to this slot";
                *desc = "Keep this curve after reboot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_SAVE_NEXT) {
                *icon = "save";
                *label = "Save as next slot";
                *desc = "Copy this curve to another slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_UNDO_ENTRY) {
                *icon = "reset";
                *label = "Undo screen edits";
                *desc = "Restore values from entry";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_RESET_EQ) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Flat bands and preamp";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_ADVANCED:
            if (row >= ADV_BAND_ROW_BASE && row < ADV_BAND_ROW_BASE + EQ_BANDS) {
                int band = row - ADV_BAND_ROW_BASE;
                *icon = "advanced";
                *label = band_labels[band];
                *desc = "Left/right fine tune, L/R coarse tune";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == ADV_ROW_PRESET_SECTION) {
                *label = "Preset controls";
                *desc = "Live edits apply now";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == ADV_ROW_PRESET_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right chooses save slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == ADV_ROW_SAVE_CURRENT) {
                *icon = "save";
                *label = "Save to this slot";
                *desc = "Keep this curve after reboot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_SAVE_NEXT) {
                *icon = "save";
                *label = "Save as next slot";
                *desc = "Copy this curve to another slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_UNDO_ENTRY) {
                *icon = "reset";
                *label = "Undo screen edits";
                *desc = "Restore values from entry";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_RESET_EQ) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Flat bands and preamp";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_THEMES:
            *icon = "themes";
            *label = eq_ui_theme_name(row);
            *desc = eq_ui_theme_description(row);
            *kind = EQ_UI_ROW_ACTION;
            break;
        case SCREEN_SETTINGS:
            if (row_is_section(screen, row)) {
                *label = "Safety and startup";
                *desc = "";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == SETTINGS_ROW_ENABLED) {
                *icon = "power";
                *label = "Equalizer";
                *desc = "Turn EQ processing on or off";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_SCOPE) {
                *icon = "speaker";
                *label = "Apply EQ to";
                *desc = "Speakers only or all outputs";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_HPF) {
                *icon = "hpf";
                *label = "Bass guard";
                *desc = "Reduces deep bass distortion";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_HEADROOM) {
                *icon = "headroom";
                *label = "Sound mode";
                *desc = "Safe, Loud, or Direct";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SETTINGS_ROW_ROUTE) {
                *icon = "route";
                *label = "Detected output";
                *desc = "Kept for games after exit";
            } else if (row == SETTINGS_ROW_STARTUP) {
                *icon = "save";
                *label = "Startup settings";
                *desc = "Used after reboot";
            }
            break;
        case SCREEN_ABOUT:
            if (row_is_section(screen, row)) {
                *label = row == ABOUT_ROW_EQ_SECTION ? "EQ basics" :
                    row == ABOUT_ROW_OUTPUT_SECTION ? "Output modes" : "Controls and paths";
                *desc = "";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == ABOUT_ROW_CREATOR) {
                *icon = "about";
                *label = "Created by shevoK";
                *desc = "EQVita app creator";
            } else if (row == ABOUT_ROW_REPOSITORY) {
                *icon = "load";
                *label = "GitHub: shev0k/EQVita";
                *desc = "Source, releases, and issues";
            } else if (row == ABOUT_ROW_LOG_FILE) {
                *icon = "save";
                *label = "Log file";
                *desc = APP_LOG_PATH;
            } else if (row == ABOUT_ROW_WHAT) {
                *icon = "simple";
                *label = "What it does";
                *desc = "Adds EQ to Vita system audio";
            } else if (row == ABOUT_ROW_VERSION) {
                *icon = "about";
                *label = "Version";
                *desc = "Companion app version";
            } else if (row == ABOUT_ROW_PLUGIN) {
                *icon = "status";
                *label = "Plugin";
                *desc = "App and plugin must match";
            } else if (row == ABOUT_ROW_PRESETS) {
                *icon = "preset";
                *label = "Presets";
                *desc = "Saved sound profiles";
            } else if (row == ABOUT_ROW_SIMPLE) {
                *icon = "simple";
                *label = "Simple EQ";
                *desc = "Fast bass, mids, treble tuning";
            } else if (row == ABOUT_ROW_ADVANCED) {
                *icon = "advanced";
                *label = "Advanced EQ";
                *desc = "Fine tune every EQ band";
            } else if (row == ABOUT_ROW_PREAMP) {
                *icon = "level";
                *label = "Preamp";
                *desc = "Volume before the EQ";
            } else if (row == ABOUT_ROW_HPF) {
                *icon = "hpf";
                *label = "HPF / Bass guard";
                *desc = "Cuts very deep bass";
            } else if (row == ABOUT_ROW_SPEAKERS) {
                *icon = "speaker";
                *label = "Speakers mode";
                *desc = "EQ only Vita speakers";
            } else if (row == ABOUT_ROW_ALL_OUTPUTS) {
                *icon = "speaker";
                *label = "All outputs";
                *desc = "EQ wired and Bluetooth too";
            } else if (row == ABOUT_ROW_BYPASS) {
                *icon = "status";
                *label = "Bypass";
                *desc = "EQ is skipped or turned off";
            } else if (row == ABOUT_ROW_DISTORTION) {
                *icon = "level";
                *label = "Avoid distortion";
                *desc = "Lower boosts if sound cracks";
            } else if (row == ABOUT_ROW_CONTROLS) {
                *icon = "nav";
                *label = "Controls";
                *desc = "Cross, Circle, Start, Triangle";
            } else if (row == ABOUT_ROW_DATA_FOLDER) {
                *icon = "save";
                *label = "Data folder";
                *desc = "ur0:data/eqvita";
            }
            break;
        default:
            break;
    }
}

static void draw_current_rows(void)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int selected = g_selected[g_screen];
    int scroll = g_scroll_top[g_screen];
    g_row_bound_count = 0;

    for (int i = 0; i < visible && scroll + i < count && i < EQ_UI_MAX_VISIBLE_ROWS; ++i) {
        int row = scroll + i;
        const char *icon;
        const char *label;
        const char *desc;
        eq_ui_row_kind_t kind;
        char value[64];
        eq_ui_row_bounds_t *bounds = &g_row_bounds[g_row_bound_count++];

        row_text(g_screen, row, &icon, &label, &desc, &kind);
        row_value(value, sizeof(value), g_screen, row);

        if (g_screen == SCREEN_SIMPLE && row >= SIMPLE_ROW_PREAMP && row <= SIMPLE_ROW_TREBLE) {
            int32_t amount = row == SIMPLE_ROW_PREAMP ? g_control.preamp_mdB :
                row == SIMPLE_ROW_BASS ? g_control.band_gain_mdB[1] :
                row == SIMPLE_ROW_MIDRANGE ? g_control.band_gain_mdB[4] : g_control.band_gain_mdB[7];
            eq_ui_draw_slider(i, row, row == selected, icon, label, desc, value, amount, kind, bounds);
        } else if (g_screen == SCREEN_ADVANCED && row >= ADV_BAND_ROW_BASE && row < ADV_BAND_ROW_BASE + EQ_BANDS) {
            int band = row - ADV_BAND_ROW_BASE;
            eq_ui_draw_slider(i, row, row == selected, icon, label, desc, value, g_control.band_gain_mdB[band], kind, bounds);
        } else {
            eq_ui_draw_row(i, row, row == selected, icon, label, desc, value, kind, bounds);
        }
    }
}

static void render_frame(void)
{
    char left[64];
    char right[64];
    char subtitle[96];
    const char *footer_left = g_screen == SCREEN_HOME ? "Circle Exit" : "Circle Back";

    if (g_plugin_compatible) {
        int status_res = EqGetStatus(&g_status);
        if (status_res < 0) {
            set_message("Status failed (%d)", status_res);
        }
    }

    snprintf(left, sizeof(left), "EQVita v%d.%d.%d", g_version.major, g_version.minor, g_version.patch);
    snprintf(right, sizeof(right), "%s - %s", eq_target_str(), g_control.enabled ? "On" : "Bypass");
    snprintf(subtitle, sizeof(subtitle), "%s - Slot %d - %s",
             g_control.enabled ? "EQ on" : "EQ off",
             g_preset_slot + 1,
             eq_target_str());

    ensure_selection_visible();
    eq_ui_begin_frame();
    eq_ui_draw_shell(screen_title(g_screen), g_screen == SCREEN_HOME ? subtitle : screen_subtitle(g_screen), left, right);
    draw_current_rows();
    eq_ui_draw_footer(footer_left, "Cross Select   Start Bypass", "Triangle Help");
    if (g_message_frames > 0 && g_message[0]) {
        eq_ui_draw_message(g_message);
        g_message_frames--;
    }
    eq_ui_end_frame();
}

static void adjust_current(int delta)
{
    int row = g_selected[g_screen];
    switch (g_screen) {
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_SLOT) {
                g_preset_slot = (g_preset_slot + (delta > 0 ? 1 : 2)) % 3;
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) adjust_preamp(delta);
            else if (row == SIMPLE_ROW_BASS) apply_simple_eq(g_control.band_gain_mdB[1] + delta, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
            else if (row == SIMPLE_ROW_MIDRANGE) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + delta, g_control.band_gain_mdB[7], 1);
            else if (row == SIMPLE_ROW_TREBLE) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + delta, 1);
            else if (row == SIMPLE_ROW_PRESET_SLOT) g_preset_slot = (g_preset_slot + (delta > 0 ? 1 : 2)) % 3;
            break;
        case SCREEN_ADVANCED:
            if (row >= ADV_BAND_ROW_BASE && row < ADV_BAND_ROW_BASE + EQ_BANDS) {
                adjust_band(row - ADV_BAND_ROW_BASE, delta);
            } else if (row == ADV_ROW_PRESET_SLOT) {
                g_preset_slot = (g_preset_slot + (delta > 0 ? 1 : 2)) % 3;
            }
            break;
        case SCREEN_THEMES: {
            move_selection(delta > 0 ? 1 : -1);
            break;
        }
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_ENABLED) toggle_enabled();
            else if (row == SETTINGS_ROW_SCOPE) toggle_speaker_only();
            else if (row == SETTINGS_ROW_HPF) toggle_hpf();
            else if (row == SETTINGS_ROW_HEADROOM) adjust_headroom_mode(delta > 0 ? 1 : -1);
            break;
        default:
            break;
    }
}

static void activate_current(void)
{
    int row = g_selected[g_screen];
    if (g_screen == SCREEN_HOME) {
        if (row == 0) {
            toggle_enabled();
            return;
        }
        change_screen(home_screen_for_row(row));
        return;
    }

    switch (g_screen) {
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_STOCK_DEPTH) apply_preset_stock_depth();
            else if (row == PRESETS_ROW_MOD_SWITCH) apply_preset_mod_switch();
            else if (row == PRESETS_ROW_SAVE) save_preset_with_message();
            else if (row == PRESETS_ROW_LOAD) load_preset_with_message();
            else if (row == PRESETS_ROW_RESET) reset_defaults();
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_SAVE_CURRENT) save_preset_with_message();
            else if (row == SIMPLE_ROW_SAVE_NEXT) save_as_next_preset();
            else if (row == SIMPLE_ROW_UNDO_ENTRY) restore_eq_entry();
            else if (row == SIMPLE_ROW_RESET_EQ) reset_defaults();
            break;
        case SCREEN_ADVANCED:
            if (row == ADV_ROW_SAVE_CURRENT) save_preset_with_message();
            else if (row == ADV_ROW_SAVE_NEXT) save_as_next_preset();
            else if (row == ADV_ROW_UNDO_ENTRY) restore_eq_entry();
            else if (row == ADV_ROW_RESET_EQ) reset_defaults();
            break;
        case SCREEN_THEMES:
            apply_theme_index(row);
            break;
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_ENABLED) toggle_enabled();
            else if (row == SETTINGS_ROW_SCOPE) toggle_speaker_only();
            else if (row == SETTINGS_ROW_HPF) toggle_hpf();
            else if (row == SETTINGS_ROW_HEADROOM) adjust_headroom_mode(1);
            break;
        default:
            break;
    }
}

static int touch_to_screen(const SceTouchReport *report, int *out_x, int *out_y)
{
    int x;
    int y;
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    if (!report || !out_x || !out_y) {
        return -1;
    }

    x = report->x;
    y = report->y;

    if (g_touch_panel_ready) {
        min_x = g_touch_panel.minDispX;
        min_y = g_touch_panel.minDispY;
        max_x = g_touch_panel.maxDispX;
        max_y = g_touch_panel.maxDispY;
        if (max_x > min_x && max_y > min_y) {
            x = ((report->x - min_x) * EQ_UI_SCREEN_W) / (max_x - min_x);
            y = ((report->y - min_y) * EQ_UI_SCREEN_H) / (max_y - min_y);
        }
    } else if (x >= EQ_UI_SCREEN_W || y >= EQ_UI_SCREEN_H) {
        x /= 2;
        y /= 2;
    }

    *out_x = clamp(x, 0, EQ_UI_SCREEN_W - 1);
    *out_y = clamp(y, 0, EQ_UI_SCREEN_H - 1);
    return 0;
}

static int row_at_point(int x, int y)
{
    for (int i = 0; i < g_row_bound_count; ++i) {
        eq_ui_row_bounds_t *b = &g_row_bounds[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            return b->row;
        }
    }
    return -1;
}

static void handle_touch(void)
{
    SceTouchData touch;
    int has_touch;
    int x = 0;
    int y = 0;

    memset(&touch, 0, sizeof(touch));
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) {
        return;
    }

    has_touch = touch.reportNum > 0;
    if (has_touch && touch_to_screen(&touch.report[0], &x, &y) < 0) {
        return;
    }

    if (has_touch && !g_touch_down) {
        int row;
        g_touch_down = 1;
        g_touch_start_x = x;
        g_touch_start_y = y;
        g_touch_last_y = y;
        g_touch_moved = 0;
        row = row_at_point(x, y);
        if (row >= 0 && row_is_selectable(g_screen, row)) {
            g_selected[g_screen] = row;
            ensure_selection_visible();
        }
    } else if (has_touch && g_touch_down) {
        int dy = y - g_touch_last_y;
        if (dy > 34 || dy < -34) {
            g_touch_moved = 1;
            scroll_current(dy > 0 ? -1 : 1);
            g_touch_last_y = y;
        }
        if ((x - g_touch_start_x > 18 || g_touch_start_x - x > 18) ||
            (y - g_touch_start_y > 18 || g_touch_start_y - y > 18)) {
            g_touch_moved = 1;
        }
    } else if (!has_touch && g_touch_down) {
        int row = row_at_point(g_touch_start_x, g_touch_start_y);
        if (!g_touch_moved && row >= 0 && row_is_selectable(g_screen, row)) {
            g_selected[g_screen] = row;
            ensure_selection_visible();
            activate_current();
        }
        g_touch_down = 0;
    }
}

int main(void)
{
    SceCtrlData last = {0};
    int repeat_timer = 0;
    int last_buttons = 0;

    eq_ui_set_theme(load_theme_index());
    if (eq_ui_init() < 0) {
        return sceKernelExitProcess(1);
    }

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    g_touch_panel_ready = (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &g_touch_panel) >= 0);

    EqGetVersion(&g_version);
    eq_control_init_defaults(&g_control);
    g_control.enabled = 1;
    g_control.route_hint = (uint8_t)detect_route_user();
    g_plugin_compatible = (g_version.major == EQ_VERSION_MAJOR && g_version.minor == EQ_VERSION_MINOR);
    if (g_plugin_compatible) {
        int load_res = load_preset();
        if (load_res < 0) {
            mark_dirty();
        }
    } else {
        memset(&g_status, 0, sizeof(g_status));
        g_status.sample_rate = 48000;
        g_status.route = EQ_ROUTE_UNKNOWN;
        g_status.bypass_reason = EQ_BYPASS_DISABLED;
    }
    app_log("start: version=%d.%d.%d compatible=%d route=%s",
        g_version.major, g_version.minor, g_version.patch,
        g_plugin_compatible, route_str(g_control.route_hint));

    while (1) {
        SceCtrlData pad;
        int newly;
        int held;
        int active_input;

        sceDisplayWaitVblankStartMulti(1);
        if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) {
            render_frame();
            continue;
        }

        newly = (~last.buttons) & pad.buttons;
        held = pad.buttons;

        if (held != last_buttons) {
            repeat_timer = 0;
            last_buttons = held;
        }

        active_input = newly;
        if (repeat_timer > 15) {
            if ((repeat_timer - 15) % 3 == 0) {
                active_input |= (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER));
            }
        }
        if (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
            repeat_timer++;
        } else {
            repeat_timer = 0;
        }

        last = pad;

        if (active_input & SCE_CTRL_UP) {
            move_selection(-1);
        } else if (active_input & SCE_CTRL_DOWN) {
            move_selection(1);
        } else if (active_input & SCE_CTRL_LEFT) {
            adjust_current(-STEP_FINE);
        } else if (active_input & SCE_CTRL_RIGHT) {
            adjust_current(STEP_FINE);
        } else if (active_input & SCE_CTRL_LTRIGGER) {
            adjust_current(-STEP_COARSE);
        } else if (active_input & SCE_CTRL_RTRIGGER) {
            adjust_current(STEP_COARSE);
        } else if (newly & SCE_CTRL_CROSS) {
            activate_current();
        } else if (newly & SCE_CTRL_CIRCLE) {
            if (g_screen == SCREEN_HOME) {
                break;
            }
            change_screen(SCREEN_HOME);
        } else if (newly & SCE_CTRL_TRIANGLE) {
            change_screen(SCREEN_ABOUT);
        } else if (newly & SCE_CTRL_START) {
            toggle_enabled();
        }

        handle_touch();
        refresh_route_hint();
        render_frame();
        maybe_log_status();
    }

    if (g_plugin_compatible) {
        save_preset();
        save_boot_state();
    }

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
    eq_ui_fini();
    return sceKernelExitProcess(0);
}
