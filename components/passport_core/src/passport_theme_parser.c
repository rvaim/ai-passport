#include "passport_theme_parser.h"

#include "passport_manifest_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    STYLE_VALUE_COLOR,
    STYLE_VALUE_UINT8,
    STYLE_VALUE_INT8,
    STYLE_VALUE_ALIGN,
} style_value_kind_t;

typedef struct {
    const char *name;
    uint8_t property;
    uint8_t offset;
    int16_t minimum;
    int16_t maximum;
    uint8_t kind;
} style_property_spec_t;

typedef struct {
    const char *name;
    passport_style_id_t id;
} style_name_spec_t;

#define COLOR_PROPERTY(name, field, property) \
    {name, property, offsetof(passport_style_t, field), 0, 0, STYLE_VALUE_COLOR}
#define UINT8_PROPERTY(name, field, property, minimum, maximum) \
    {name, property, offsetof(passport_style_t, field), minimum, maximum, STYLE_VALUE_UINT8}
#define INT8_PROPERTY(name, field, property, minimum, maximum) \
    {name, property, offsetof(passport_style_t, field), minimum, maximum, STYLE_VALUE_INT8}

static const style_property_spec_t s_property_specs[] = {
    COLOR_PROPERTY("background_color", background_color,
                   PASSPORT_STYLE_PROP_BACKGROUND_COLOR),
    UINT8_PROPERTY("background_opacity", background_opacity,
                   PASSPORT_STYLE_PROP_BACKGROUND_OPACITY, 0, 255),
    UINT8_PROPERTY("opacity", opacity, PASSPORT_STYLE_PROP_OPACITY, 0, 255),
    UINT8_PROPERTY("radius", radius, PASSPORT_STYLE_PROP_RADIUS, 0, 32),
    COLOR_PROPERTY("border_color", border_color, PASSPORT_STYLE_PROP_BORDER_COLOR),
    UINT8_PROPERTY("border_width", border_width,
                   PASSPORT_STYLE_PROP_BORDER_WIDTH, 0, 4),
    UINT8_PROPERTY("border_opacity", border_opacity,
                   PASSPORT_STYLE_PROP_BORDER_OPACITY, 0, 255),
    COLOR_PROPERTY("shadow_color", shadow_color, PASSPORT_STYLE_PROP_SHADOW_COLOR),
    UINT8_PROPERTY("shadow_width", shadow_width,
                   PASSPORT_STYLE_PROP_SHADOW_WIDTH, 0, 12),
    UINT8_PROPERTY("shadow_spread", shadow_spread,
                   PASSPORT_STYLE_PROP_SHADOW_SPREAD, 0, 6),
    UINT8_PROPERTY("shadow_opacity", shadow_opacity,
                   PASSPORT_STYLE_PROP_SHADOW_OPACITY, 0, 255),
    INT8_PROPERTY("shadow_offset_x", shadow_offset_x,
                  PASSPORT_STYLE_PROP_SHADOW_OFFSET_X, -8, 8),
    INT8_PROPERTY("shadow_offset_y", shadow_offset_y,
                  PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y, -8, 8),
    UINT8_PROPERTY("padding", padding, PASSPORT_STYLE_PROP_PADDING, 0, 24),
    UINT8_PROPERTY("gap", gap, PASSPORT_STYLE_PROP_GAP, 0, 24),
    COLOR_PROPERTY("text_color", text_color, PASSPORT_STYLE_PROP_TEXT_COLOR),
    UINT8_PROPERTY("text_opacity", text_opacity,
                   PASSPORT_STYLE_PROP_TEXT_OPACITY, 0, 255),
    {"text_align", PASSPORT_STYLE_PROP_TEXT_ALIGN,
     offsetof(passport_style_t, text_align), 0, 0, STYLE_VALUE_ALIGN},
    INT8_PROPERTY("text_line_spacing", text_line_spacing,
                  PASSPORT_STYLE_PROP_TEXT_LINE_SPACING, -8, 16),
    COLOR_PROPERTY("line_color", line_color, PASSPORT_STYLE_PROP_LINE_COLOR),
    UINT8_PROPERTY("line_opacity", line_opacity,
                   PASSPORT_STYLE_PROP_LINE_OPACITY, 0, 255),
    UINT8_PROPERTY("line_width", line_width,
                   PASSPORT_STYLE_PROP_LINE_WIDTH, 0, 8),
    COLOR_PROPERTY("arc_color", arc_color, PASSPORT_STYLE_PROP_ARC_COLOR),
    UINT8_PROPERTY("arc_opacity", arc_opacity,
                   PASSPORT_STYLE_PROP_ARC_OPACITY, 0, 255),
    UINT8_PROPERTY("arc_width", arc_width,
                   PASSPORT_STYLE_PROP_ARC_WIDTH, 0, 16),
};

static const style_name_spec_t s_style_names[] = {
    {"view", PASSPORT_STYLE_VIEW},
    {"page", PASSPORT_STYLE_PAGE},
    {"surface", PASSPORT_STYLE_SURFACE},
    {"text", PASSPORT_STYLE_TEXT},
    {"muted_text", PASSPORT_STYLE_MUTED_TEXT},
    {"accent_text", PASSPORT_STYLE_ACCENT_TEXT},
    {"card", PASSPORT_STYLE_CARD},
    {"button", PASSPORT_STYLE_BUTTON},
    {"button_pressed", PASSPORT_STYLE_BUTTON_PRESSED},
    {"image", PASSPORT_STYLE_IMAGE},
    {"list", PASSPORT_STYLE_LIST},
    {"list_item", PASSPORT_STYLE_LIST_ITEM},
    {"list_item_selected", PASSPORT_STYLE_LIST_ITEM_SELECTED},
    {"bar", PASSPORT_STYLE_BAR},
    {"indicator", PASSPORT_STYLE_INDICATOR},
    {"arc", PASSPORT_STYLE_ARC},
    {"slider", PASSPORT_STYLE_SLIDER},
    {"knob", PASSPORT_STYLE_KNOB},
    {"switch", PASSPORT_STYLE_SWITCH},
    {"spinner", PASSPORT_STYLE_SPINNER},
    {"line", PASSPORT_STYLE_LINE},
    {"checkbox", PASSPORT_STYLE_CHECKBOX},
    {"canvas", PASSPORT_STYLE_CANVAS},
    {"divider", PASSPORT_STYLE_DIVIDER},
};

#undef COLOR_PROPERTY
#undef UINT8_PROPERTY
#undef INT8_PROPERTY

_Static_assert(PASSPORT_STYLE_PROP_COUNT <= 64,
               "style presence mask must contain every public property");
_Static_assert(sizeof(passport_style_t) <= UINT8_MAX,
               "style property offsets must fit in uint8_t");

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parse_color(const cJSON *item, uint32_t *out)
{
    if (!cJSON_IsString(item) || !item->valuestring ||
        strlen(item->valuestring) != 7U || item->valuestring[0] != '#') {
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

static bool parse_bounded_integer(const cJSON *item, int minimum, int maximum,
                                  int *out)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < minimum ||
        item->valuedouble > maximum || item->valuedouble != (double)item->valueint) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static const style_property_spec_t *find_property(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(s_property_specs) / sizeof(s_property_specs[0]); ++i) {
        if (strcmp(name, s_property_specs[i].name) == 0) return &s_property_specs[i];
    }
    return NULL;
}

static bool find_style(const char *name, passport_style_id_t *out)
{
    if (!name || !out) return false;
    for (size_t i = 0; i < sizeof(s_style_names) / sizeof(s_style_names[0]); ++i) {
        if (strcmp(name, s_style_names[i].name) == 0) {
            *out = s_style_names[i].id;
            return true;
        }
    }
    return false;
}

static bool parse_align(const cJSON *item, uint8_t *out)
{
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    if (strcmp(item->valuestring, "left") == 0) *out = PASSPORT_TEXT_ALIGN_LEFT;
    else if (strcmp(item->valuestring, "center") == 0) *out = PASSPORT_TEXT_ALIGN_CENTER;
    else if (strcmp(item->valuestring, "right") == 0) *out = PASSPORT_TEXT_ALIGN_RIGHT;
    else return false;
    return true;
}

static bool parse_style(const cJSON *object, passport_style_t *out)
{
    if (!cJSON_IsObject(object) || !object->child) return false;
    passport_style_t next = {0};
    for (const cJSON *child = object->child; child; child = child->next) {
        const style_property_spec_t *spec = find_property(child->string);
        if (!spec) return false;
        uint64_t bit = UINT64_C(1) << spec->property;
        if ((next.present & bit) != 0U) return false;

        uint8_t *destination = (uint8_t *)&next + spec->offset;
        if (spec->kind == STYLE_VALUE_COLOR) {
            uint32_t value;
            if (!parse_color(child, &value)) return false;
            memcpy(destination, &value, sizeof(value));
        } else if (spec->kind == STYLE_VALUE_ALIGN) {
            if (!parse_align(child, destination)) return false;
        } else {
            int value;
            if (!parse_bounded_integer(child, spec->minimum, spec->maximum, &value)) {
                return false;
            }
            if (spec->kind == STYLE_VALUE_INT8) {
                int8_t converted = (int8_t)value;
                memcpy(destination, &converted, sizeof(converted));
            } else {
                uint8_t converted = (uint8_t)value;
                memcpy(destination, &converted, sizeof(converted));
            }
        }
        next.present |= bit;
    }
    *out = next;
    return true;
}

static bool parse_styles(const cJSON *styles, passport_theme_definition_t *out)
{
    if (!cJSON_IsObject(styles) || !styles->child) return false;
    passport_theme_definition_t next = {0};
    bool seen[PASSPORT_STYLE_COUNT] = {false};
    for (const cJSON *child = styles->child; child; child = child->next) {
        passport_style_id_t id;
        if (!find_style(child->string, &id) || seen[id] ||
            !parse_style(child, &next.styles[id])) {
            return false;
        }
        seen[id] = true;
    }
    *out = next;
    return true;
}

esp_err_t passport_theme_parse_manifest_json(const char *json, size_t length,
                                             passport_manifest_t *manifest_out,
                                             passport_theme_definition_t *theme_out)
{
    if (!json || !manifest_out || !theme_out) return ESP_ERR_INVALID_ARG;

    passport_manifest_t manifest;
    cJSON *document = NULL;
    esp_err_t err = passport_manifest_parse_document(
        json, length, PASSPORT_PACKAGE_THEME, &manifest, &document);
    if (err != ESP_OK) return err;
    cJSON *styles = cJSON_GetObjectItemCaseSensitive(document, "styles");
    passport_theme_definition_t next = {0};
    bool ok = parse_styles(styles, &next);
    if (ok) {
        *manifest_out = manifest;
        *theme_out = next;
    }
    cJSON_Delete(document);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
