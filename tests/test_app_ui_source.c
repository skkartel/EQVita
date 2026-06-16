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

static int range_contains(const char *start, const char *end, const char *needle)
{
    size_t needle_len;
    if (!start || !end || !needle || end < start) {
        return 0;
    }

    needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }

    for (const char *p = start; p + needle_len <= end; ++p) {
        if (strncmp(p, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_ui_waits_for_rendering_before_resource_teardown(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/ui_vita.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "vita2d_wait_rendering_done()") != NULL);
    ASSERT_TRUE(strstr(source, "wait_for_render_idle") != NULL);
    free(source);
}

static void test_advanced_eq_exposes_preamp_control(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "#define ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "row == ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "g_screen == SCREEN_ADVANCED && row == ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "adjust_preamp(delta)") != NULL);
    free(source);
}

static void test_app_log_reports_audio_timing_budget(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "audio-budget") != NULL);
    ASSERT_TRUE(strstr(source, "budget_us") != NULL);
    ASSERT_TRUE(strstr(source, "debug_last_us") != NULL);
    ASSERT_TRUE(strstr(source, "debug_max_us") != NULL);
    free(source);
}

static void test_audio_budget_log_flags_current_block_not_stale_max(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static void maybe_log_status(void)");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static void ");
    ASSERT_TRUE(fn_end != NULL);

    ASSERT_TRUE(range_contains(fn_start, fn_end, "g_status.debug_last_us > budget_us"));
    ASSERT_TRUE(!range_contains(fn_start, fn_end, "g_status.debug_max_us > budget_us"));

    free(source);
}

static void test_app_log_has_run_ids_and_diagnostic_drain(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "g_log_run_id") != NULL);
    ASSERT_TRUE(strstr(source, "run_id=") != NULL);
    ASSERT_TRUE(strstr(source, "EqDrainDiagnostics") != NULL);
    ASSERT_TRUE(strstr(source, "diag-core: run=") != NULL);
    ASSERT_TRUE(strstr(source, "diag-level: run=") != NULL);
    ASSERT_TRUE(strstr(source, "evt=") != NULL);
    ASSERT_TRUE(strstr(source, "gen=") != NULL);
    ASSERT_TRUE(strstr(source, "type=") != NULL);
    ASSERT_TRUE(strstr(source, "in_peak=") != NULL);
    ASSERT_TRUE(strstr(source, "out_peak=") != NULL);
    ASSERT_TRUE(strstr(source, "eff_preamp=") != NULL);
    free(source);
}

static void test_diagnostic_logs_are_split_to_avoid_truncation(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(!strstr(source, "app_log(\"diag: run=%08x seq=%u evt=%s port=%d gen=%u type=%s len=%u sr=%u ch=%u route=%s reason=%s ret=%d elapsed_us=%u budget_us=%u clips=%d"));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-core: run=%08x seq=%u evt=%s port=%d gen=%u type=%s len=%u sr=%u ch=%u route=%s reason=%s ret=%d\""));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-time: run=%08x seq=%u elapsed_us=%u budget_us=%u dirty=%u flags=0x%02x\""));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-level: run=%08x seq=%u clips=%d in_peak=%u/%u out_peak=%u/%u headroom=%s preamp=%d eff_preamp=%d max_boost=%d hpf=%u\""));

    free(source);
}

static void test_status_log_includes_slowest_block_context(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "max_port=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_len=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_sr=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_ch=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_budget_us=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_route=%s") != NULL);
    ASSERT_TRUE(strstr(source, "max_reason=%s") != NULL);
    ASSERT_TRUE(strstr(source, "max_clips=%d") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-stage: run=%08x control_us=%u registry_us=%u route_us=%u copy_in_us=%u retarget_us=%u dsp_us=%u copy_out_us=%u original_us=%u status_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-time: run=%08x last_total_us=%u last_budget_us=%u last_margin_us=%d max_total_us=%u max_dsp_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin: run=%08x min_margin_us=%d min_total_us=%u min_budget_us=%u min_port=%u min_len=%u min_sr=%u min_ch=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin-stage: run=%08x min_route=%s min_reason=%s dsp_us=%u original_us=%u status_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin-len: run=%08x len256_us=%d len1024_us=%d len2048_us=%d\"") != NULL);
    ASSERT_TRUE(strstr(source, "g_status.debug_max_us != last_max_us") != NULL);

    free(source);
}

static void test_status_log_splits_slowest_block_context(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "app_log(\"status: run=%08x route=%s active=%u reason=%s") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-max: run=%08x max_us=%u max_budget_us=%u") != NULL);
    ASSERT_TRUE(!strstr(source, "status: run=%08x route=%s active=%u reason=%s sr=%u port=%u len=%u ch=%u runs=%u ports=%u busy=%u unknown=%u last_us=%u max_us=%u max_port=%u"));

    free(source);
}

static void test_builtin_presets_persist_active_slot_after_apply(void)
{
    char path[512];
    char *source;
    char *stock;
    char *mod;
    char *stock_end;
    char *mod_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    stock = strstr(source, "static void apply_preset_stock_depth(void)");
    mod = strstr(source, "static void apply_preset_mod_switch(void)");
    ASSERT_TRUE(stock != NULL);
    ASSERT_TRUE(mod != NULL);

    stock_end = strstr(stock + 1, "static void ");
    mod_end = strstr(mod + 1, "static void ");
    ASSERT_TRUE(stock_end != NULL);
    ASSERT_TRUE(mod_end != NULL);

    ASSERT_TRUE(range_contains(stock, stock_end, "save_preset()"));
    ASSERT_TRUE(range_contains(stock, stock_end, "persist_active_preset_state(\"preset-stock-depth\""));
    ASSERT_TRUE(range_contains(stock, stock_end, "eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE)"));
    ASSERT_TRUE(range_contains(mod, mod_end, "save_preset()"));
    ASSERT_TRUE(range_contains(mod, mod_end, "persist_active_preset_state(\"preset-mod-switch\""));

    free(source);
}

static void test_reset_defaults_restores_safe_headroom(void)
{
    char path[512];
    char *source;
    char *reset;
    char *reset_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    reset = strstr(source, "static void reset_defaults(void)");
    ASSERT_TRUE(reset != NULL);
    reset_end = strstr(reset + 1, "static void ");
    ASSERT_TRUE(reset_end != NULL);

    ASSERT_TRUE(range_contains(reset, reset_end, "eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE)"));

    free(source);
}

int main(void)
{
    test_ui_waits_for_rendering_before_resource_teardown();
    test_advanced_eq_exposes_preamp_control();
    test_app_log_reports_audio_timing_budget();
    test_audio_budget_log_flags_current_block_not_stale_max();
    test_app_log_has_run_ids_and_diagnostic_drain();
    test_diagnostic_logs_are_split_to_avoid_truncation();
    test_status_log_includes_slowest_block_context();
    test_status_log_splits_slowest_block_context();
    test_builtin_presets_persist_active_slot_after_apply();
    test_reset_defaults_restores_safe_headroom();
    return 0;
}
