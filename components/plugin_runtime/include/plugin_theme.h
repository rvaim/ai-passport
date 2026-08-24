#pragma once

#include <stddef.h>
#include <stdint.h>

#define PLUGIN_THEME_MAGIC "THM1"
#define PLUGIN_THEME_VERSION 1U
#define PLUGIN_THEME_SIZE 64U
#define PLUGIN_THEME_COLOR_REFERENCE_FLAG UINT32_C(0xFF000000)
#define PLUGIN_THEME_COLOR_REFERENCE_MASK UINT32_C(0xFF000000)

typedef enum {
    PLUGIN_THEME_COLOR_BACKGROUND = 0,
    PLUGIN_THEME_COLOR_SURFACE,
    PLUGIN_THEME_COLOR_TEXT,
    PLUGIN_THEME_COLOR_TEXT_MUTED,
    PLUGIN_THEME_COLOR_ACCENT,
    PLUGIN_THEME_COLOR_ACCENT_STRONG,
    PLUGIN_THEME_COLOR_SELECTION,
    PLUGIN_THEME_COLOR_MUTED_SURFACE,
    PLUGIN_THEME_COLOR_DANGER,
    PLUGIN_THEME_COLOR_SUCCESS,
    PLUGIN_THEME_COLOR_BORDER,
    PLUGIN_THEME_COLOR_SELECTION_BORDER,
    PLUGIN_THEME_COLOR_COUNT,
} plugin_theme_color_t;

typedef enum {
    PLUGIN_THEME_DECORATION_NONE = 0,
    PLUGIN_THEME_DECORATION_PIXEL_GROUND = 1,
} plugin_theme_decoration_t;

typedef struct {
    uint32_t colors[PLUGIN_THEME_COLOR_COUNT];
    uint8_t panel_radius;
    uint8_t panel_border_width;
    uint8_t panel_shadow_width;
    int8_t panel_shadow_offset_x;
    int8_t panel_shadow_offset_y;
    uint8_t decoration;
} plugin_theme_descriptor_t;

typedef enum {
    PLUGIN_THEME_OK = 0,
    PLUGIN_THEME_INVALID_ARGUMENT,
    PLUGIN_THEME_TRUNCATED,
    PLUGIN_THEME_BAD_MAGIC,
    PLUGIN_THEME_UNSUPPORTED_VERSION,
    PLUGIN_THEME_INVALID_VALUE,
} plugin_theme_result_t;

plugin_theme_result_t plugin_theme_parse(const uint8_t *data, size_t size,
                                         plugin_theme_descriptor_t *theme);
