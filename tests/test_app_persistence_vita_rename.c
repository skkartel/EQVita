#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int real_rename_for_test(const char *from, const char *to)
{
    return rename(from, to);
}

static int vita_like_rename_for_test(const char *from, const char *to)
{
    if (access(to, F_OK) == 0) {
        errno = EEXIST;
        return -1;
    }
    return real_rename_for_test(from, to);
}

#define EQVITA_HOST_TESTS 1
#define rename vita_like_rename_for_test
#include "../app/persistence.c"
#undef rename

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_I32(actual, expected) do { \
    int a__ = (int)(actual); \
    int e__ = (int)(expected); \
    if (a__ != e__) { \
        fprintf(stderr, "ASSERT_EQ_I32 failed at %s:%d: got %d expected %d\n", __FILE__, __LINE__, a__, e__); \
        exit(1); \
    } \
} while (0)

static char *make_temp_dir(void)
{
    char templ[] = "/tmp/eqvita-vita-rename-XXXXXX";
    char *dir = strdup(templ);

    ASSERT_TRUE(dir != NULL);
    ASSERT_TRUE(mkdtemp(dir) != NULL);
    return dir;
}

static void test_preset_save_replaces_existing_file_when_rename_cannot_overwrite(void)
{
    char *dir = make_temp_dir();
    eq_control_t first;
    eq_control_t second;
    eq_control_t loaded;

    eq_control_init_defaults(&first);
    first.enabled = 1;
    first.band_gain_mdB[2] = 1500;

    eq_control_init_defaults(&second);
    second.enabled = 1;
    second.band_gain_mdB[2] = 6500;

    ASSERT_TRUE(eqvita_save_preset(dir, 0, &first) == 0);
    ASSERT_TRUE(eqvita_save_preset(dir, 0, &second) == 0);
    ASSERT_TRUE(eqvita_load_preset(dir, 0, &loaded, NULL) == 0);
    ASSERT_EQ_I32(loaded.band_gain_mdB[2], 6500);

    free(dir);
}

static void test_theme_save_replaces_existing_file_when_rename_cannot_overwrite(void)
{
    char *dir = make_temp_dir();
    int theme = -1;

    ASSERT_TRUE(eqvita_save_theme_index(dir, 3) == 0);
    ASSERT_TRUE(eqvita_save_theme_index(dir, 13) == 0);
    ASSERT_TRUE(eqvita_load_theme_index(dir, 16, 0, &theme) == 0);
    ASSERT_EQ_I32(theme, 13);

    free(dir);
}

int main(void)
{
    test_preset_save_replaces_existing_file_when_rename_cannot_overwrite();
    test_theme_save_replaces_existing_file_when_rename_cannot_overwrite();
    return 0;
}
