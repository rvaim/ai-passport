#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_THEME_ID_MAX 48
#define PASSPORT_THEME_NAME_MAX 48
#define PASSPORT_MAX_INSTALLED_THEMES 8

/** Public, fixed style graph shared by native pages and PAP applications. */
typedef enum {
    PASSPORT_STYLE_VIEW = 0,
    PASSPORT_STYLE_PAGE,
    PASSPORT_STYLE_SURFACE,
    PASSPORT_STYLE_TEXT,
    PASSPORT_STYLE_MUTED_TEXT,
    PASSPORT_STYLE_ACCENT_TEXT,
    PASSPORT_STYLE_CARD,
    PASSPORT_STYLE_BUTTON,
    PASSPORT_STYLE_BUTTON_PRESSED,
    PASSPORT_STYLE_IMAGE,
    PASSPORT_STYLE_LIST,
    PASSPORT_STYLE_LIST_ITEM,
    PASSPORT_STYLE_LIST_ITEM_SELECTED,
    PASSPORT_STYLE_BAR,
    PASSPORT_STYLE_INDICATOR,
    PASSPORT_STYLE_ARC,
    PASSPORT_STYLE_SLIDER,
    PASSPORT_STYLE_KNOB,
    PASSPORT_STYLE_SWITCH,
    PASSPORT_STYLE_SPINNER,
    PASSPORT_STYLE_LINE,
    PASSPORT_STYLE_CHECKBOX,
    PASSPORT_STYLE_CANVAS,
    PASSPORT_STYLE_DIVIDER,
    PASSPORT_STYLE_COUNT,
} passport_style_id_t;

typedef enum {
    PASSPORT_STYLE_PROP_BACKGROUND_COLOR = 0,
    PASSPORT_STYLE_PROP_BACKGROUND_OPACITY,
    PASSPORT_STYLE_PROP_OPACITY,
    PASSPORT_STYLE_PROP_RADIUS,
    PASSPORT_STYLE_PROP_BORDER_COLOR,
    PASSPORT_STYLE_PROP_BORDER_WIDTH,
    PASSPORT_STYLE_PROP_BORDER_OPACITY,
    PASSPORT_STYLE_PROP_SHADOW_COLOR,
    PASSPORT_STYLE_PROP_SHADOW_WIDTH,
    PASSPORT_STYLE_PROP_SHADOW_SPREAD,
    PASSPORT_STYLE_PROP_SHADOW_OPACITY,
    PASSPORT_STYLE_PROP_SHADOW_OFFSET_X,
    PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y,
    PASSPORT_STYLE_PROP_PADDING,
    PASSPORT_STYLE_PROP_GAP,
    PASSPORT_STYLE_PROP_TEXT_COLOR,
    PASSPORT_STYLE_PROP_TEXT_OPACITY,
    PASSPORT_STYLE_PROP_TEXT_ALIGN,
    PASSPORT_STYLE_PROP_TEXT_LINE_SPACING,
    PASSPORT_STYLE_PROP_LINE_COLOR,
    PASSPORT_STYLE_PROP_LINE_OPACITY,
    PASSPORT_STYLE_PROP_LINE_WIDTH,
    PASSPORT_STYLE_PROP_ARC_COLOR,
    PASSPORT_STYLE_PROP_ARC_OPACITY,
    PASSPORT_STYLE_PROP_ARC_WIDTH,
    PASSPORT_STYLE_PROP_COUNT,
} passport_style_property_t;

typedef enum {
    PASSPORT_TEXT_ALIGN_LEFT = 0,
    PASSPORT_TEXT_ALIGN_CENTER,
    PASSPORT_TEXT_ALIGN_RIGHT,
} passport_text_align_t;

typedef struct {
    uint64_t present;
    uint32_t background_color;
    uint32_t border_color;
    uint32_t shadow_color;
    uint32_t text_color;
    uint32_t line_color;
    uint32_t arc_color;
    uint8_t background_opacity;
    uint8_t opacity;
    uint8_t radius;
    uint8_t border_width;
    uint8_t border_opacity;
    uint8_t shadow_width;
    uint8_t shadow_spread;
    uint8_t shadow_opacity;
    int8_t shadow_offset_x;
    int8_t shadow_offset_y;
    uint8_t padding;
    uint8_t gap;
    uint8_t text_opacity;
    uint8_t text_align;
    int8_t text_line_spacing;
    uint8_t line_opacity;
    uint8_t line_width;
    uint8_t arc_opacity;
    uint8_t arc_width;
} passport_style_t;

typedef struct {
    passport_style_t styles[PASSPORT_STYLE_COUNT];
} passport_theme_definition_t;

typedef struct {
    char id[PASSPORT_THEME_ID_MAX];
    char name[PASSPORT_THEME_NAME_MAX];
} passport_theme_info_t;

/** Initialize the current theme from the persisted selection or built-in default. */
esp_err_t passport_theme_init(void);
const passport_style_t *passport_theme_style(passport_style_id_t id);
const char *passport_theme_current_id(void);

/** Apply an installed theme by ID and persist the selection. */
esp_err_t passport_theme_apply(const char *id);

/** Enumerate installed theme manifests. */
size_t passport_theme_list(passport_theme_info_t *out, size_t capacity);
