#include "passport_theme_parser.h"

#include "passport_manifest_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    THEME_TOKEN_COLOR,
    THEME_TOKEN_UINT8,
    THEME_TOKEN_INT8,
} theme_token_kind_t;

typedef struct {
    const char *name;
    uint8_t offset;
    int16_t minimum;
    int16_t maximum;
    uint8_t kind;
} theme_token_spec_t;

#define COLOR_TOKEN(field) \
    {#field, offsetof(passport_theme_tokens_t, field), 0, 0, THEME_TOKEN_COLOR}
#define UINT8_TOKEN(field, minimum, maximum) \
    {#field, offsetof(passport_theme_tokens_t, field), minimum, maximum, THEME_TOKEN_UINT8}
#define INT8_TOKEN(field, minimum, maximum) \
    {#field, offsetof(passport_theme_tokens_t, field), minimum, maximum, THEME_TOKEN_INT8}

static const theme_token_spec_t s_token_specs[] = {
    COLOR_TOKEN(background),
    COLOR_TOKEN(surface),
    COLOR_TOKEN(item_background),
    COLOR_TOKEN(text),
    COLOR_TOKEN(muted_text),
    COLOR_TOKEN(accent),
    COLOR_TOKEN(selection_text),
    COLOR_TOKEN(divider),
    COLOR_TOKEN(border),
    COLOR_TOKEN(shadow),
    UINT8_TOKEN(spacing, 2, 12),
    UINT8_TOKEN(radius, 0, 32),
    UINT8_TOKEN(border_width, 0, 4),
    UINT8_TOKEN(shadow_width, 0, 12),
    UINT8_TOKEN(shadow_spread, 0, 6),
    UINT8_TOKEN(shadow_opacity, 0, 255),
    INT8_TOKEN(shadow_offset_x, -8, 8),
    INT8_TOKEN(shadow_offset_y, -8, 8),
};

#undef COLOR_TOKEN
#undef UINT8_TOKEN
#undef INT8_TOKEN

_Static_assert(sizeof(passport_theme_tokens_t) <= UINT8_MAX,
               "theme token offsets must fit in uint8_t");

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parse_color(cJSON *item, uint32_t *out)
{
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] != '#') {
        return false;
    }
    uint32_t color = 0;
    for (size_t i = 1; i <= 6U; ++i) {
        int digit = hex_digit(item->valuestring[i]);
        if (digit < 0) return false;
        color = (color << 4) | (uint32_t)digit;
    }
    if (item->valuestring[7] != '\0') return false;
    *out = color;
    return true;
}

static bool parse_bounded_integer(cJSON *item, int minimum, int maximum, int *out)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < minimum ||
        item->valuedouble > maximum || item->valuedouble != (double)item->valueint) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static const theme_token_spec_t *find_token_spec(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(s_token_specs) / sizeof(s_token_specs[0]); ++i) {
        if (strcmp(name, s_token_specs[i].name) == 0) return &s_token_specs[i];
    }
    return NULL;
}

static bool tokens_have_exact_schema(const cJSON *tokens)
{
    if (!cJSON_IsObject(tokens)) return false;
    size_t count = 0;
    for (const cJSON *child = tokens->child; child; child = child->next) {
        if (!find_token_spec(child->string)) return false;
        ++count;
    }
    return count == sizeof(s_token_specs) / sizeof(s_token_specs[0]);
}

static bool parse_tokens(cJSON *tokens, passport_theme_tokens_t *out)
{
    if (!tokens_have_exact_schema(tokens)) return false;

    for (size_t i = 0; i < sizeof(s_token_specs) / sizeof(s_token_specs[0]); ++i) {
        const theme_token_spec_t *spec = &s_token_specs[i];
        cJSON *item = cJSON_GetObjectItemCaseSensitive(tokens, spec->name);
        uint8_t *destination = (uint8_t *)out + spec->offset;
        if (spec->kind == THEME_TOKEN_COLOR) {
            uint32_t value;
            if (!parse_color(item, &value)) return false;
            memcpy(destination, &value, sizeof(value));
            continue;
        }

        int value;
        if (!parse_bounded_integer(item, spec->minimum, spec->maximum, &value)) {
            return false;
        }
        if (spec->kind == THEME_TOKEN_INT8) {
            int8_t converted = (int8_t)value;
            memcpy(destination, &converted, sizeof(converted));
        } else {
            uint8_t converted = (uint8_t)value;
            memcpy(destination, &converted, sizeof(converted));
        }
    }
    return true;
}

esp_err_t passport_theme_parse_manifest_json(const char *json, size_t length,
                                             passport_manifest_t *manifest_out,
                                             passport_theme_tokens_t *tokens_out)
{
    if (!json || !manifest_out || !tokens_out) return ESP_ERR_INVALID_ARG;

    passport_manifest_t manifest;
    cJSON *document = NULL;
    esp_err_t err = passport_manifest_parse_document(
        json, length, PASSPORT_PACKAGE_THEME, &manifest, &document);
    if (err != ESP_OK) return err;
    cJSON *tokens = cJSON_GetObjectItemCaseSensitive(document, "tokens");
    passport_theme_tokens_t next = {0};
    bool ok = parse_tokens(tokens, &next);
    if (ok) {
        *manifest_out = manifest;
        *tokens_out = next;
    }
    cJSON_Delete(document);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
