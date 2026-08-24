#include "plugin_theme.h"

#include "plugin_format.h"

#include <string.h>

plugin_theme_result_t plugin_theme_parse(const uint8_t *data, size_t size,
                                         plugin_theme_descriptor_t *theme)
{
    if (!data || !theme) return PLUGIN_THEME_INVALID_ARGUMENT;
    if (size < PLUGIN_THEME_SIZE) return PLUGIN_THEME_TRUNCATED;
    if (size != PLUGIN_THEME_SIZE || memcmp(data, PLUGIN_THEME_MAGIC, 4U) != 0) {
        return PLUGIN_THEME_BAD_MAGIC;
    }
    if (plugin_format_read_u16(data + 4U) != PLUGIN_THEME_VERSION ||
        plugin_format_read_u16(data + 6U) != PLUGIN_THEME_SIZE) {
        return PLUGIN_THEME_UNSUPPORTED_VERSION;
    }

    memset(theme, 0, sizeof(*theme));
    for (size_t index = 0; index < PLUGIN_THEME_COLOR_COUNT; ++index) {
        uint32_t color = plugin_format_read_u32(data + 8U + index * 4U);
        if (color > 0xffffffU) return PLUGIN_THEME_INVALID_VALUE;
        theme->colors[index] = color;
    }
    theme->panel_radius = data[56U];
    theme->panel_border_width = data[57U];
    theme->panel_shadow_width = data[58U];
    theme->panel_shadow_offset_x = (int8_t)data[59U];
    theme->panel_shadow_offset_y = (int8_t)data[60U];
    theme->decoration = data[61U];

    if (theme->panel_radius > 16U || theme->panel_border_width > 6U ||
        theme->panel_shadow_width > 12U ||
        theme->panel_shadow_offset_x < -12 || theme->panel_shadow_offset_x > 12 ||
        theme->panel_shadow_offset_y < -12 || theme->panel_shadow_offset_y > 12 ||
        theme->decoration > PLUGIN_THEME_DECORATION_PIXEL_GROUND ||
        data[62U] != 0U || data[63U] != 0U) {
        return PLUGIN_THEME_INVALID_VALUE;
    }
    return PLUGIN_THEME_OK;
}
