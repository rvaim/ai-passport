#pragma once

#include "bsp_button.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_SERVICE_DISPLAY   (1U << 0)
#define APP_SERVICE_STORAGE   (1U << 1)
#define APP_SERVICE_AUDIO     (1U << 2)
#define APP_SERVICE_NEARBY    (1U << 3)
#define APP_SERVICE_IDENTITY  (1U << 4)
#define APP_SERVICE_SETTINGS  (1U << 5)

typedef enum {
    APP_PLUGIN_SYSTEM,
    APP_PLUGIN_PACKAGE,
} app_plugin_kind_t;

typedef struct {
    const char *id;
    const char *name;
    const char *icon;
    app_plugin_kind_t kind;
    bool pinned;
    bool available;
} app_plugin_info_t;

esp_err_t app_registry_init(uint32_t available_services);
void app_registry_refresh(void);
size_t app_registry_count(void);
size_t app_registry_pinned_count(void);
const app_plugin_info_t *app_registry_get(size_t index);
esp_err_t app_registry_enter(size_t index);
void app_registry_exit(void);
void app_registry_key(bsp_btn_t button, bsp_btn_ev_t event);
bool app_registry_active(void);
bool app_registry_take_home_request(void);
