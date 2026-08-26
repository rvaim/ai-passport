#pragma once

#include "esp_err.h"
#include <stddef.h>

#define PASSPORT_FS_ROOT "/passport"
#define PASSPORT_APPS_DIR PASSPORT_FS_ROOT "/apps"
#define PASSPORT_THEMES_DIR PASSPORT_FS_ROOT "/themes"
#define PASSPORT_STAGING_DIR PASSPORT_FS_ROOT "/.staging"
#define PASSPORT_INCOMING_PACKAGE PASSPORT_FS_ROOT "/.incoming.pap"

/** Mount the wear-levelled FAT partition labelled "appfs" and create system directories. */
esp_err_t passport_storage_init(void);

/** Recursively delete a file or directory below /passport. */
esp_err_t passport_storage_remove_tree(const char *path);

/** Ensure a directory exists. Parents must already exist. */
esp_err_t passport_storage_ensure_dir(const char *path);

/** Read a small UTF-8 text file with an explicit caller-owned buffer. */
esp_err_t passport_storage_read_text(const char *path, char *buf, size_t capacity, size_t *out_len);
