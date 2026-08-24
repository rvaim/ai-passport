#pragma once

#include "plugin_theme.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t ui_theme_init(void);
bool ui_theme_refresh(void);
size_t ui_theme_count(void);
size_t ui_theme_active_index(void);
const char *ui_theme_name(size_t index);
const char *ui_theme_active_name(void);
bool ui_theme_is_active(const char *plugin_id);
esp_err_t ui_theme_select(size_t index);
esp_err_t ui_theme_select_next(int32_t *index);
uint32_t ui_theme_generation(void);
uint32_t ui_theme_color(plugin_theme_color_t token);
const plugin_theme_descriptor_t *ui_theme_descriptor(void);
