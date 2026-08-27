#pragma once

#include "passport_package.h"
#include "passport_theme.h"
#include "esp_err.h"
#include <stddef.h>

/**
 * Parse one exact current-schema theme manifest and its sparse style layers.
 * theme_out may be NULL when the caller only needs full schema validation.
 */
esp_err_t passport_theme_parse_manifest_json(const char *json, size_t length,
                                             passport_manifest_t *manifest_out,
                                             passport_theme_definition_t *theme_out);
