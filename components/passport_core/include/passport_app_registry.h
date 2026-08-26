#pragma once

#include "esp_err.h"
#include "passport_package.h"
#include <stddef.h>

#define PASSPORT_MAX_INSTALLED_APPS 16

typedef struct {
    passport_manifest_t manifest;
    char root[160];
} passport_app_info_t;

/** Rescan /passport/apps. Invalid or incomplete directories are ignored. */
esp_err_t passport_app_registry_scan(void);
size_t passport_app_registry_count(void);
const passport_app_info_t *passport_app_registry_get(size_t index);
