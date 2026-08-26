#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_THEME_ID_MAX 48
#define PASSPORT_THEME_NAME_MAX 48
#define PASSPORT_MAX_INSTALLED_THEMES 8

typedef struct {
    uint32_t background;
    uint32_t surface;
    uint32_t text;
    uint32_t muted_text;
    uint32_t accent;
    uint32_t divider;
    uint8_t spacing;
    uint8_t radius;
} passport_theme_tokens_t;

typedef struct {
    char id[PASSPORT_THEME_ID_MAX];
    char name[PASSPORT_THEME_NAME_MAX];
} passport_theme_info_t;

/** Load persisted theme selection or fall back to the built-in compact theme. */
esp_err_t passport_theme_init(void);
const passport_theme_tokens_t *passport_theme_current(void);
const char *passport_theme_current_id(void);

/** Apply an installed theme by ID and persist the selection. */
esp_err_t passport_theme_apply(const char *id);

/** Enumerate installed theme manifests. */
size_t passport_theme_list(passport_theme_info_t *out, size_t capacity);
