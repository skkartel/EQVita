#pragma once

#include <stddef.h>

#define EQVITA_MEDIA_MAX_PATH 512
#define EQVITA_MEDIA_MAX_NAME 128
#define EQVITA_MEDIA_MAX_ENTRIES 128

typedef enum eqvita_media_entry_kind
{
    EQVITA_MEDIA_ENTRY_PARENT = 0,
    EQVITA_MEDIA_ENTRY_DIRECTORY,
    EQVITA_MEDIA_ENTRY_FILE
} eqvita_media_entry_kind_t;

typedef struct eqvita_media_entry
{
    eqvita_media_entry_kind_t kind;
    char name[EQVITA_MEDIA_MAX_NAME];
    char path[EQVITA_MEDIA_MAX_PATH];
} eqvita_media_entry_t;

typedef struct eqvita_media_listing
{
    char path[EQVITA_MEDIA_MAX_PATH];
    eqvita_media_entry_t entries[EQVITA_MEDIA_MAX_ENTRIES];
    int count;
} eqvita_media_listing_t;

int eqvita_media_browser_is_supported_file(const char *path);
const char *eqvita_media_browser_file_name(const char *path);
int eqvita_media_browser_join_path(char *out, size_t out_size, const char *dir, const char *name);
int eqvita_media_browser_parent_path(char *out, size_t out_size, const char *path);
int eqvita_media_browser_is_root_path(const char *path);
int eqvita_media_browser_read_roots(eqvita_media_listing_t *listing);
int eqvita_media_browser_read_dir(eqvita_media_listing_t *listing, const char *path);
