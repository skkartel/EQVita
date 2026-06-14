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
    ASSERT_TRUE(range_contains(mod, mod_end, "save_preset()"));
    ASSERT_TRUE(range_contains(mod, mod_end, "persist_active_preset_state(\"preset-mod-switch\""));

    free(source);
}

int main(void)
{
    test_ui_waits_for_rendering_before_resource_teardown();
    test_advanced_eq_exposes_preamp_control();
    test_app_log_reports_audio_timing_budget();
    test_builtin_presets_persist_active_slot_after_apply();
    return 0;
}
