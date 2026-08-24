#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include "lvgl.h"

#include <stdbool.h>

esp_err_t system_plugins_init(bool battery_available);

void system_settings_enter(void);
void system_settings_exit(void);
void system_settings_key(bsp_btn_t button, bsp_btn_ev_t event);
bool system_settings_back(void);
lv_obj_t *system_device_info_screen_create(void);
