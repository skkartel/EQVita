#include <psp2/ctrl.h>
#include <psp2/avconfig.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/system_param.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "debug_screen/debugScreen.h"
#include "../common/eq_shared.h"

#define X_MARGIN 16
#define Y_MARGIN 16
#define UI_BUF 512
#define STEP_FINE 500
#define STEP_COARSE 1000
#define PRESET_SYNC_FAILED -4
#define PRESET_PATH_FMT "ur0:data/eqvita/preset%d.eqvp"
#define LEGACY_PRESET_PATH_FMT "ur0:data/eqvita/preset%d.bin"
#define BOOT_STATE_PATH "ur0:data/eqvita/boot.eqbs"
#define APP_LOG_PATH "ur0:data/eqvita/app.log"
#define STATUS_LOG_INTERVAL_FRAMES 60

#define SCE_AVCONFIG_VOLCTRL_ONBOARD 1
#define SCE_AVCONFIG_VOLCTRL_BLUETOOTH 2
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_0 0x001
#define SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT 0x004
#define SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT 0x010
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_8 0x100

int sceAVConfigGetConnectedAudioDevice(uint32_t *flags);
int sceAVConfigGetVolCtrlEnable(uint32_t *volCtrl, int *muted, int *avls);

// Colors
#define COL_RESET "\e[0m"
#define COL_HEADER "\e[1;36m"
#define COL_SECTION "\e[1;33m"
#define COL_SELECTED "\e[7m"
#define COL_VALUE "\e[1;37m"
#define COL_METER_L "\e[32m"
#define COL_METER_M "\e[33m"
#define COL_METER_H "\e[31m"

static const char *band_labels[EQ_BANDS] = {
    "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
};

static eq_control_t g_control;
static eq_status_t g_status;
static eq_version_t g_version;
static int g_selected = 0;
static int g_preset_slot = 0;
static int g_scroll_top = 0;
static int g_view_mode = 0;
static int g_plugin_compatible = 0;
static char g_message[96];
static int g_message_frames = 0;
static uint32_t g_status_log_frames = 0;

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ensure_data_dir(void) {
    sceIoMkdir("ur0:data", 0777);
    sceIoMkdir("ur0:data/eqvita", 0777);
}

static void app_log(const char *fmt, ...) {
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

static const char *route_str(uint8_t r) {
    switch (r) {
        case EQ_ROUTE_SPEAKER: return "Speaker";
        case EQ_ROUTE_HEADPHONES: return "Headphones";
        case EQ_ROUTE_BLUETOOTH: return "Bluetooth";
        default: return "Unknown";
    }
}

static const char *bypass_reason_str(uint8_t r) {
    switch (r) {
        case EQ_BYPASS_NONE: return "Active";
        case EQ_BYPASS_DISABLED: return "Disabled";
        case EQ_BYPASS_SPEAKER_ONLY: return "Speaker-only";
        case EQ_BYPASS_UNKNOWN_ROUTE: return "Unknown route";
        case EQ_BYPASS_INVALID_PORT: return "Invalid port";
        case EQ_BYPASS_BUFFER_TOO_LARGE: return "Large buffer";
        case EQ_BYPASS_COPY_FAILED: return "Copy failed";
        case EQ_BYPASS_UNSUPPORTED_FORMAT: return "Unsupported";
        case EQ_BYPASS_AUDIO_BUSY: return "Audio busy";
        default: return "Bypassed";
    }
}

static const char *headroom_mode_str(uint8_t mode) {
    switch (mode) {
        case EQ_HEADROOM_LOUD: return "LOUD";
        case EQ_HEADROOM_RAW: return "RAW ";
        case EQ_HEADROOM_SAFE:
        default: return "SAFE";
    }
}

static void set_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_message, sizeof(g_message), fmt, ap);
    va_end(ap);
    g_message_frames = 180;
    app_log("message: %s", g_message);
}

static eq_route_t detect_route_user(void) {
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

static void maybe_log_status(void) {
    static uint32_t last_counter = 0;
    static uint8_t last_route = 0xffu;
    static uint8_t last_reason = 0xffu;
    static uint8_t last_active = 0xffu;
    int changed = 0;
    int force_log = 0;

    if (!g_plugin_compatible) {
        return;
    }

    g_status_log_frames++;
    force_log = (g_status_log_frames >= STATUS_LOG_INTERVAL_FRAMES);
    changed = (g_status.status_counter != last_counter ||
        g_status.route != last_route ||
        g_status.bypass_reason != last_reason ||
        g_status.eq_active != last_active);

    if (g_status_log_frames < STATUS_LOG_INTERVAL_FRAMES &&
        !changed) {
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

static void ui_init(void) {
    PsvDebugScreenFont *font = psvDebugScreenGetFont();
    font = psvDebugScreenScaleFont2x(font);
    psvDebugScreenSetFont(font);
    psvDebugScreenSetBgColor(0x000000);
    psvDebugScreenSetFgColor(0xFFFFFF);

    // Control defaults are initialized in main before plugin sync.
}

static void ui_reset(void) {
    psvDebugScreenBlank(0x00);
    psvDebugScreenSetCoordsXY((int[]){X_MARGIN}, (int[]){Y_MARGIN});
}

static void ui_line(const char *fmt, ...) {
    char buf[UI_BUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    psvDebugScreenPuts(buf);
}

static int mark_dirty(void) {
    if (!g_plugin_compatible) {
        return -1;
    }
    g_control.route_hint = (uint8_t)detect_route_user();
    g_control.dirty_counter++;
    int set_res = EqSetControl(&g_control);
    int status_res = (set_res >= 0) ? EqGetStatus(&g_status) : -1;
    if (set_res < 0 || status_res < 0) {
        set_message("Plugin communication failed (%d/%d)", set_res, status_res);
    }
    return set_res;
}

static void refresh_route_hint(void) {
    if (!g_plugin_compatible) {
        return;
    }
    uint8_t route = (uint8_t)detect_route_user();
    if (g_control.route_hint != route) {
        g_control.route_hint = route;
        g_control.dirty_counter++;
        int res = EqSetControl(&g_control);
        if (res < 0) {
            set_message("Route update failed (%d)", res);
        }
    }
}

static void toggle_enabled(void) {
    g_control.enabled = !g_control.enabled;
    mark_dirty();
}

static void toggle_speaker_only(void) {
    g_control.speaker_only = !g_control.speaker_only;
    mark_dirty();
}

static void toggle_hpf(void) {
    eq_control_set_hpf_enabled(&g_control, !eq_control_hpf_enabled(&g_control));
    mark_dirty();
}

static void adjust_headroom_mode(int delta) {
    int mode = (int)eq_control_get_headroom_mode(&g_control) + delta;
    if (mode < 0) mode = EQ_HEADROOM_RAW;
    if (mode > EQ_HEADROOM_RAW) mode = EQ_HEADROOM_SAFE;
    eq_control_set_headroom_mode(&g_control, (uint8_t)mode);
    mark_dirty();
}

static void adjust_preamp(int delta) {
    int v = g_control.preamp_mdB + delta;
    g_control.preamp_mdB = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static void adjust_band(int idx, int delta) {
    int v = g_control.band_gain_mdB[idx] + delta;
    g_control.band_gain_mdB[idx] = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static int save_preset(void) {
    ensure_data_dir();
    char path[64];
    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    eq_preset_file_t preset;
    eq_preset_build(&preset, &g_control);
    int written = sceIoWrite(fd, &preset, sizeof(preset));
    int close_res = sceIoClose(fd);
    if (written != (int)sizeof(preset)) {
        return written < 0 ? written : -1;
    }
    return close_res;
}

static int save_boot_state(void) {
    ensure_data_dir();
    eq_control_t boot_control = g_control;
    boot_control.route_hint = (uint8_t)detect_route_user();

    SceUID fd = sceIoOpen(BOOT_STATE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    eq_boot_state_file_t state;
    eq_boot_state_build(&state, &boot_control);
    int written = sceIoWrite(fd, &state, sizeof(state));
    int close_res = sceIoClose(fd);
    if (written != (int)sizeof(state)) {
        return written < 0 ? written : -1;
    }
    return close_res;
}

static int load_preset(void) {
    char path[64];
    eq_control_t previous = g_control;
    eq_preset_primary_status_t primary_status = EQ_PRESET_PRIMARY_MISSING;

    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
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

static void save_preset_with_message(void) {
    int res = save_preset();
    if (res >= 0) {
        int boot_res = save_boot_state();
        if (boot_res >= 0) {
            set_message("Saved preset %d", g_preset_slot + 1);
        } else {
            set_message("Saved preset %d, boot save failed (%d)", g_preset_slot + 1, boot_res);
        }
    } else {
        set_message("Save failed (%d)", res);
    }
}

static void load_preset_with_message(void) {
    int res = load_preset();
    if (res > 0) {
        set_message("Imported legacy preset %d", g_preset_slot + 1);
    } else if (res == 0) {
        set_message("Loaded preset %d", g_preset_slot + 1);
    } else if (res == PRESET_SYNC_FAILED) {
        /* mark_dirty already reported the plugin communication failure. */
    } else {
        set_message("Load failed (%d)", res);
    }
}

static void reset_defaults(void) {
    g_control.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        g_control.band_gain_mdB[i] = 0;
    }
    mark_dirty();
}

static void apply_simple_eq(int bass, int mid, int treble, int auto_preamp) {
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

static void apply_preset_stock_depth(void) {
    g_control.preamp_mdB = -4000;
    g_control.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) g_control.band_gain_mdB[i] = 4000;
    for (int i = 4; i <= 6; ++i) g_control.band_gain_mdB[i] = -2000;
    for (int i = 7; i <= 9; ++i) g_control.band_gain_mdB[i] = 2000;
    if (mark_dirty() == 0) {
        set_message("Applied STOCK Depth");
    }
}

static void apply_preset_mod_switch(void) {
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

static void draw_bar(int mdB) {
    int val = mdB / 1000;
    if (val < -12) val = -12;
    if (val > 12) val = 12;
    
    char bar[22];
    memset(bar, ' ', 21);
    bar[21] = 0;
    bar[10] = '|';

    if (val > 0) {
        int len = (val * 10) / 12;
        for (int i = 0; i < len; ++i) bar[11 + i] = '=';
    } else if (val < 0) {
        int len = (-val * 10) / 12;
        for (int i = 0; i < len; ++i) bar[9 - i] = '=';
    }
    
    psvDebugScreenPuts("[");
    psvDebugScreenPuts(bar);
    psvDebugScreenPuts("]");
}

static void draw_meter_colored(uint16_t peak) {
    int val = (peak * 20) / 32767;
    if (val > 20) val = 20;
    
    psvDebugScreenPuts("[");
    for (int i = 0; i < 20; ++i) {
        if (i < val) {
            if (i < 12) psvDebugScreenPuts(COL_METER_L "#");
            else if (i < 16) psvDebugScreenPuts(COL_METER_M "#");
            else psvDebugScreenPuts(COL_METER_H "#");
        } else {
            psvDebugScreenPuts(" ");
        }
    }
    psvDebugScreenPuts(COL_RESET "]");
}

static void ui_render(void) {
    ui_reset();
    
    // Header
    ui_line(COL_HEADER "EQ Vita v%d.%d.%d [%s MODE]" COL_RESET "\n", 
        g_version.major, g_version.minor, g_version.patch, 
        g_view_mode == 0 ? "SIMPLE" : "ADVANCED");
    
    // Status Line
    if (g_plugin_compatible) {
        int status_res = EqGetStatus(&g_status);
        if (status_res < 0) {
            set_message("Status failed (%d)", status_res);
        }
    }
    ui_line("Route: %s (%s) | SR: %u | EQ: %s/%s | %s\n",
        route_str(g_status.route),
        g_control.speaker_only ? "Spk" : "All",
        g_status.sample_rate,
        g_control.enabled ? "On" : "Off",
        g_status.eq_active ? "Act" : "Byp",
        g_status.smoothing ? "Transition" : bypass_reason_str(g_status.bypass_reason));
    ui_line("Ports:%u Busy:%u Unknown:%u DSP:%uus/%uus\n",
        g_status.debug_active_ports,
        g_status.debug_busy_bypass_count,
        g_status.debug_unknown_port_count,
        g_status.debug_last_us,
        g_status.debug_max_us);
    ui_line("Log: ur0:data/eqvita/app.log\n");
    if (!g_plugin_compatible) {
        ui_line(COL_METER_H "Plugin mismatch. Install EQVita plugin v%d.%d.x." COL_RESET "\n",
            EQ_VERSION_MAJOR, EQ_VERSION_MINOR);
    } else if (g_message_frames > 0 && g_message[0]) {
        ui_line(COL_VALUE "%s" COL_RESET "\n", g_message);
        g_message_frames--;
    }
    ui_line("------------------------------------------------\n");

    // Viewport Calculation
    int total_items = (g_view_mode == 0) ? 14 : 19;
    int viewport_height = 12;
    
    // Auto-scroll
    if (g_selected < g_scroll_top) g_scroll_top = g_selected;
    if (g_selected >= g_scroll_top + viewport_height) g_scroll_top = g_selected - viewport_height + 1;

    // Render List
    for (int i = g_scroll_top; i < g_scroll_top + viewport_height && i < total_items; ++i) {
        int is_sel = (i == g_selected);
        const char *sel_prefix = is_sel ? COL_SELECTED "> " : "  ";
        const char *sel_suffix = is_sel ? COL_RESET : "";

        // Settings Section
        if (i == 0) {
            ui_line(COL_SECTION "[SETTINGS]" COL_RESET "\n");
            ui_line("%sEnabled:      [%s]%s\n", sel_prefix, g_control.enabled?"ON ":"OFF", sel_suffix);
        } else if (i == 1) {
            ui_line("%sSpeaker only: [%s]%s\n", sel_prefix, g_control.speaker_only?"YES":"NO ", sel_suffix);
        } else if (i == 2) {
            ui_line("%sHPF (70Hz):   [%s]%s\n", sel_prefix, eq_control_hpf_enabled(&g_control)?"ON ":"OFF", sel_suffix);
        } else if (i == 3) {
            ui_line("%sHeadroom:     [%s]%s\n", sel_prefix, headroom_mode_str(eq_control_get_headroom_mode(&g_control)), sel_suffix);
        } else if (i == 4) {
            ui_line("%sPreamp:       %+5.1f dB%s\n", sel_prefix, g_control.preamp_mdB/1000.0f, sel_suffix);
        }
        // EQ Section
        else if (g_view_mode == 0) {
            if (i == 5) {
                ui_line("\n");
                ui_line(COL_SECTION "[EQUALIZER]" COL_RESET "\n");
                psvDebugScreenPrintf("%sBass:    ", sel_prefix);
                draw_bar(g_control.band_gain_mdB[1]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[1]/1000.0f, sel_suffix);
            } else if (i == 6) {
                psvDebugScreenPrintf("%sMidrange:", sel_prefix);
                draw_bar(g_control.band_gain_mdB[4]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[4]/1000.0f, sel_suffix);
            } else if (i == 7) {
                psvDebugScreenPrintf("%sTreble:  ", sel_prefix);
                draw_bar(g_control.band_gain_mdB[7]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[7]/1000.0f, sel_suffix);
            }
        } else {
            if (i == 5) {
                ui_line("\n");
                ui_line(COL_SECTION "[EQUALIZER]" COL_RESET "\n");
            }
            if (i >= 5 && i < 15) {
                int band_idx = i - 5;
                psvDebugScreenPrintf("%s%4s Hz ", sel_prefix, band_labels[band_idx]);
                draw_bar(g_control.band_gain_mdB[band_idx]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[band_idx]/1000.0f, sel_suffix);
            }
        }
        
        // Actions Section
        int action_start = (g_view_mode == 0) ? 8 : 15;
        if (g_view_mode == 0) {
            if (i == action_start) {
                ui_line("\n");
                ui_line(COL_SECTION "[ACTIONS]" COL_RESET "\n");
                ui_line("%sPreset Slot:  [%d]%s\n", sel_prefix, g_preset_slot + 1, sel_suffix);
            } else if (i == action_start + 1) {
                ui_line("%s[ Preset: STOCK Depth ]%s\n", sel_prefix, sel_suffix);
            } else if (i == action_start + 2) {
                ui_line("%s[ Preset: MOD Switch ]%s\n", sel_prefix, sel_suffix);
            } else if (i == action_start + 3) {
                ui_line("%s[ Save Preset   ]%s\n", sel_prefix, sel_suffix);
            } else if (i == action_start + 4) {
                ui_line("%s[ Load Preset   ]%s\n", sel_prefix, sel_suffix);
            } else if (i == action_start + 5) {
                ui_line("%s[ Reset EQ      ]%s\n", sel_prefix, sel_suffix);
            }
        } else if (i == action_start) {
            ui_line("\n");
            ui_line(COL_SECTION "[ACTIONS]" COL_RESET "\n");
            ui_line("%sPreset Slot:  [%d]%s\n", sel_prefix, g_preset_slot + 1, sel_suffix);
        } else if (i == action_start + 1) {
            ui_line("%s[ Save Preset ]%s\n", sel_prefix, sel_suffix);
        } else if (i == action_start + 2) {
            ui_line("%s[ Load Preset ]%s\n", sel_prefix, sel_suffix);
        } else if (i == action_start + 3) {
            ui_line("%s[ Reset EQ    ]%s\n", sel_prefix, sel_suffix);
        }
    }
    
    // Peak Meters (Fixed at bottom)
    ui_line("\n" COL_SECTION "[PEAK LEVELS]" COL_RESET "\n");
    ui_line("L: "); draw_meter_colored(g_status.peak_l); ui_line("\n");
    ui_line("R: "); draw_meter_colored(g_status.peak_r); ui_line(" Clips: %d\n", g_status.clip_events);

    ui_line("------------------------------------------------\n");
    ui_line("[HELP] " COL_VALUE "X" COL_RESET ":Toggle " COL_VALUE "O" COL_RESET ":Exit " COL_VALUE "Start" COL_RESET ":Bypass " COL_VALUE "Select" COL_RESET ":View\n");
}

int main(void) {
    psvDebugScreenInit();
    psvDebugScreenSetBgColor(0x000000);
    psvDebugScreenSetFgColor(0xFFFFFF);
    psvDebugScreenPuts("EQ Vita starting...\n");

    EqGetVersion(&g_version);
    psvDebugScreenPrintf("Version %d.%d.%d\n", g_version.major, g_version.minor, g_version.patch);
    psvDebugScreenSwapFb();

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

    for (int i = 0; i < 60; ++i) { sceDisplayWaitVblankStartMulti(1); }

    ui_init();
    psvDebugScreenSwapFb();

#define REPEAT_DELAY 15
#define REPEAT_RATE 3

    SceCtrlData last = {0};
    int repeat_timer = 0;
    int last_buttons = 0;

    while (1) {
        sceDisplayWaitVblankStartMulti(1);

        SceCtrlData pad;
        if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) { continue; }
        
        int newly = (~last.buttons) & pad.buttons;
        int held = pad.buttons;
        
        if (held != last_buttons) {
            repeat_timer = 0;
            last_buttons = held;
        }

        int active_input = newly;
        
        if (repeat_timer > REPEAT_DELAY) {
            if ((repeat_timer - REPEAT_DELAY) % REPEAT_RATE == 0) {
                active_input |= (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER));
            }
        }
        if (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
            repeat_timer++;
        } else {
            repeat_timer = 0;
        }

        last = pad;

        if (!g_plugin_compatible) {
            if (newly & SCE_CTRL_CIRCLE) {
                break;
            }
            ui_render();
            maybe_log_status();
            psvDebugScreenSwapFb();
            continue;
        }

        int rows_simple = 5 + 3 + 6; // 14 items (0-13)
        int rows_advanced = 5 + EQ_BANDS + 4; // 19 items (0-18)
        int rows = (g_view_mode == 0) ? rows_simple : rows_advanced;

        if (active_input & SCE_CTRL_UP) {
            g_selected = (g_selected - 1 + rows) % rows;
        } else if (active_input & SCE_CTRL_DOWN) {
            g_selected = (g_selected + 1) % rows;
        } else if (active_input & SCE_CTRL_LEFT) {
            if (g_selected == 0 && (newly & SCE_CTRL_LEFT)) toggle_enabled();
            else if (g_selected == 1 && (newly & SCE_CTRL_LEFT)) toggle_speaker_only();
            else if (g_selected == 2 && (newly & SCE_CTRL_LEFT)) toggle_hpf();
            else if (g_selected == 3 && (newly & SCE_CTRL_LEFT)) adjust_headroom_mode(-1);
            else if (g_selected == 4) adjust_preamp(-STEP_FINE);
            else if (g_view_mode == 0) { // Simple Mode
                if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1] - STEP_FINE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] - STEP_FINE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 7) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] - STEP_FINE, 1);
                else if (g_selected == 8 && (newly & SCE_CTRL_LEFT)) { g_preset_slot = (g_preset_slot + 2) % 3; }
            } else { // Advanced Mode
                if (g_selected >= 5 && g_selected < 5 + EQ_BANDS) {
                    adjust_band(g_selected - 5, -STEP_FINE);
                } else if (g_selected == 5 + EQ_BANDS && (newly & SCE_CTRL_LEFT)) {
                    g_preset_slot = (g_preset_slot + 2) % 3;
                }
            }
        } else if (active_input & SCE_CTRL_RIGHT) {
            if (g_selected == 0 && (newly & SCE_CTRL_RIGHT)) toggle_enabled();
            else if (g_selected == 1 && (newly & SCE_CTRL_RIGHT)) toggle_speaker_only();
            else if (g_selected == 2 && (newly & SCE_CTRL_RIGHT)) toggle_hpf();
            else if (g_selected == 3 && (newly & SCE_CTRL_RIGHT)) adjust_headroom_mode(1);
            else if (g_selected == 4) adjust_preamp(STEP_FINE);
            else if (g_view_mode == 0) { // Simple Mode
                if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1] + STEP_FINE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + STEP_FINE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 7) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + STEP_FINE, 1);
                else if (g_selected == 8 && (newly & SCE_CTRL_RIGHT)) { g_preset_slot = (g_preset_slot + 1) % 3; }
            } else { // Advanced Mode
                if (g_selected >= 5 && g_selected < 5 + EQ_BANDS) {
                    adjust_band(g_selected - 5, STEP_FINE);
                } else if (g_selected == 5 + EQ_BANDS && (newly & SCE_CTRL_RIGHT)) {
                    g_preset_slot = (g_preset_slot + 1) % 3;
                }
            }
        } else if (active_input & SCE_CTRL_LTRIGGER) {
            if (g_selected == 4) adjust_preamp(-STEP_COARSE);
            else if (g_view_mode == 0) {
                if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1] - STEP_COARSE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] - STEP_COARSE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 7) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] - STEP_COARSE, 1);
            } else {
                if (g_selected >= 5 && g_selected < 5 + EQ_BANDS) {
                    adjust_band(g_selected - 5, -STEP_COARSE);
                }
            }
        } else if (active_input & SCE_CTRL_RTRIGGER) {
            if (g_selected == 4) adjust_preamp(STEP_COARSE);
            else if (g_view_mode == 0) {
                if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1] + STEP_COARSE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + STEP_COARSE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 7) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + STEP_COARSE, 1);
            } else {
                if (g_selected >= 5 && g_selected < 5 + EQ_BANDS) {
                    adjust_band(g_selected - 5, STEP_COARSE);
                }
            }
        } else if (newly & SCE_CTRL_CROSS) {
            if (g_selected == 0) toggle_enabled();
            else if (g_selected == 1) toggle_speaker_only();
            else if (g_selected == 2) toggle_hpf();
            else if (g_selected == 3) adjust_headroom_mode(1);
            else if (g_view_mode == 0) { // Simple
                if (g_selected == 9) apply_preset_stock_depth();
                else if (g_selected == 10) apply_preset_mod_switch();
                else if (g_selected == 11) save_preset_with_message();
                else if (g_selected == 12) load_preset_with_message();
                else if (g_selected == 13) reset_defaults();
            } else { // Advanced
                int action_start = 15;
                if (g_selected == action_start + 1) save_preset_with_message();
                else if (g_selected == action_start + 2) load_preset_with_message();
                else if (g_selected == action_start + 3) reset_defaults();
            }
        } else if (newly & SCE_CTRL_SELECT) {
            g_view_mode = !g_view_mode;
            g_selected = 0;
            g_scroll_top = 0;
        } else if (newly & SCE_CTRL_START) {
            toggle_enabled();
        } else if (newly & SCE_CTRL_CIRCLE) {
            break;
        }

        refresh_route_hint();
        ui_render();
        maybe_log_status();
        psvDebugScreenSwapFb();
    }

    if (g_plugin_compatible) {
        save_preset();
        save_boot_state();
    }

    return sceKernelExitProcess(0);
}
