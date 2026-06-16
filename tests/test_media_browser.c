#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../app/media_browser.h"

static void test_supported_music_extensions(void)
{
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.ogg"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.OGG"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.mp3"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.WAV"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song.flac"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song.txt"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song"));
    assert(!eqvita_media_browser_is_supported_file(NULL));
}

static void test_safe_path_join(void)
{
    char out[EQVITA_MEDIA_MAX_PATH];

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:", "music") == 0);
    assert(strcmp(out, "ux0:music") == 0);

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:music", "song.ogg") == 0);
    assert(strcmp(out, "ux0:music/song.ogg") == 0);

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:/music/", "song.ogg") == 0);
    assert(strcmp(out, "ux0:/music/song.ogg") == 0);

    assert(eqvita_media_browser_join_path(out, 8, "ux0:/music", "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(NULL, sizeof(out), "ux0:/music", "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(out, sizeof(out), NULL, "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:/music", NULL) < 0);
}

static void test_file_name_from_path(void)
{
    assert(strcmp(eqvita_media_browser_file_name("ux0:music/song.ogg"), "song.ogg") == 0);
    assert(strcmp(eqvita_media_browser_file_name("uma0:music/song.mp3"), "song.mp3") == 0);
    assert(strcmp(eqvita_media_browser_file_name("song.wav"), "song.wav") == 0);
    assert(strcmp(eqvita_media_browser_file_name(NULL), "") == 0);
}

static void test_parent_path(void)
{
    char out[EQVITA_MEDIA_MAX_PATH];

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:music/song.ogg") == 0);
    assert(strcmp(out, "ux0:music/") == 0);

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:music/") == 0);
    assert(strcmp(out, "ux0:") == 0);

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:") < 0);
    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:/") < 0);
}

static void test_root_path_detection(void)
{
    assert(eqvita_media_browser_is_root_path("ux0:"));
    assert(eqvita_media_browser_is_root_path("ux0:/"));
    assert(eqvita_media_browser_is_root_path("ur0:"));
    assert(!eqvita_media_browser_is_root_path("ux0:music/"));
    assert(!eqvita_media_browser_is_root_path("Storage"));
    assert(!eqvita_media_browser_is_root_path(NULL));
}

static void test_roots_include_music_shortcut(void)
{
    eqvita_media_listing_t listing;
    int found_ux0 = 0;
    int found_music = 0;

    assert(eqvita_media_browser_read_roots(&listing) >= 2);
    for (int i = 0; i < listing.count; ++i) {
        if (strcmp(listing.entries[i].name, "ux0:") == 0 &&
            strcmp(listing.entries[i].path, "ux0:") == 0) {
            found_ux0 = 1;
        }
        if (strcmp(listing.entries[i].name, "ux0:music/") == 0 &&
            strcmp(listing.entries[i].path, "ux0:music/") == 0) {
            found_music = 1;
        }
    }
    assert(found_ux0);
    assert(found_music);
}

int main(void)
{
    test_supported_music_extensions();
    test_safe_path_join();
    test_file_name_from_path();
    test_parent_path();
    test_root_path_detection();
    test_roots_include_music_shortcut();
    puts("media_browser tests passed");
    return 0;
}
