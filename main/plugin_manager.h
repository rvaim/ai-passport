#pragma once

#include "bsp_button.h"
#include "esp_err.h"

#include <stdbool.h>

esp_err_t plugin_manager_init(void);
void plugin_manager_enter(void);
void plugin_manager_exit(void);
void plugin_manager_key(bsp_btn_t button, bsp_btn_ev_t event);
bool plugin_manager_back(void);
