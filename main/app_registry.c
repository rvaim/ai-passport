#include "app_registry.h"

#include "plugin_manager.h"
#include "plugin_host.h"
#include "plugin_store.h"
#include "system_plugins.h"

#include "esp_log.h"
#include "lvgl.h"

#include <string.h>

typedef struct {
    app_plugin_info_t info;
    uint32_t requires;
    void (*enter)(void);
    void (*exit)(void);
    void (*key)(bsp_btn_t button, bsp_btn_ev_t event);
    bool (*back)(void);
    bool replaceable;
} system_plugin_t;

typedef struct {
    app_plugin_info_t info;
    const system_plugin_t *system;
    plugin_record_t *package;
} registry_entry_t;

static const system_plugin_t SYSTEM_PLUGINS[] = {
    {
        .info = {
            .id = "system.settings",
            .name = "设置",
            .icon = LV_SYMBOL_SETTINGS,
            .kind = APP_PLUGIN_SYSTEM,
            .pinned = true,
        },
        .requires = APP_SERVICE_DISPLAY,
        .enter = system_settings_enter,
        .exit = system_settings_exit,
        .key = system_settings_key,
        .back = system_settings_back,
        .replaceable = true,
    },
    {
        .info = {
            .id = "system.plugins",
            .name = "插件",
            .icon = LV_SYMBOL_DOWNLOAD,
            .kind = APP_PLUGIN_SYSTEM,
            .pinned = true,
        },
        .requires = APP_SERVICE_DISPLAY | APP_SERVICE_STORAGE |
                    APP_SERVICE_NEARBY | APP_SERVICE_IDENTITY,
        .enter = plugin_manager_enter,
        .exit = plugin_manager_exit,
        .key = plugin_manager_key,
        .back = plugin_manager_back,
    },
};

#define SYSTEM_PLUGIN_COUNT (sizeof(SYSTEM_PLUGINS) / sizeof(SYSTEM_PLUGINS[0]))
#define REGISTRY_CAPACITY (SYSTEM_PLUGIN_COUNT + PLUGIN_STORE_MAX_ACTIVE)

static registry_entry_t s_entries[REGISTRY_CAPACITY];
static plugin_record_t s_packages[PLUGIN_STORE_MAX_ACTIVE];
static const char *TAG = "app_registry";
static uint32_t s_available_services;
static size_t s_count;
static int s_active = -1;
static bool s_active_package;
static bool s_home_requested;

static void sort_packages(size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        plugin_record_t value = s_packages[index];
        size_t cursor = index;
        while (cursor > 0U &&
               strcmp(s_packages[cursor - 1U].manifest.name, value.manifest.name) > 0) {
            s_packages[cursor] = s_packages[cursor - 1U];
            --cursor;
        }
        s_packages[cursor] = value;
    }
}

static int system_index_for(const char *plugin_id)
{
    for (size_t index = 0; index < SYSTEM_PLUGIN_COUNT; ++index) {
        if (strcmp(SYSTEM_PLUGINS[index].info.id, plugin_id) == 0) return (int)index;
    }
    return -1;
}

static bool package_available(const plugin_record_t *record)
{
    if ((record->manifest.permissions & PLUGIN_PERMISSION_AUDIO) != 0U &&
        (s_available_services & APP_SERVICE_AUDIO) == 0U) {
        return false;
    }
    if ((record->manifest.permissions & PLUGIN_PERMISSION_MICROPHONE) != 0U &&
        (s_available_services & APP_SERVICE_AUDIO) == 0U) {
        return false;
    }
    if ((record->manifest.permissions & PLUGIN_PERMISSION_NEARBY) != 0U &&
        (s_available_services & APP_SERVICE_NEARBY) == 0U) {
        return false;
    }
    if ((record->manifest.permissions & PLUGIN_PERMISSION_SETTINGS) != 0U &&
        (s_available_services & APP_SERVICE_SETTINGS) == 0U) {
        return false;
    }
    return true;
}

esp_err_t app_registry_init(uint32_t available_services)
{
    s_available_services = available_services;
    app_registry_refresh();
    return ESP_OK;
}

void app_registry_refresh(void)
{
    if (s_active >= 0) return;
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t index = 0; index < SYSTEM_PLUGIN_COUNT; ++index) {
        s_entries[index].info = SYSTEM_PLUGINS[index].info;
        s_entries[index].info.available =
            (SYSTEM_PLUGINS[index].requires & ~s_available_services) == 0U;
        s_entries[index].system = &SYSTEM_PLUGINS[index];
    }

    size_t package_count = plugin_store_list(s_packages, PLUGIN_STORE_MAX_ACTIVE);
    if (package_count > PLUGIN_STORE_MAX_ACTIVE) package_count = PLUGIN_STORE_MAX_ACTIVE;
    sort_packages(package_count);

    // Replaceable system entries keep their pinned position and built-in fallback.
    for (size_t index = 0; index < package_count; ++index) {
        plugin_record_t *record = &s_packages[index];
        if (record->manifest.kind != PLUGIN_KIND_APP) continue;
        int system_index = system_index_for(record->manifest.id);
        if (system_index < 0) continue;
        const system_plugin_t *system = &SYSTEM_PLUGINS[system_index];
        if (!system->replaceable) {
            ESP_LOGW(TAG, "ignoring package for non-replaceable system plugin %s",
                     record->manifest.id);
            continue;
        }
        if ((record->manifest.permissions & PLUGIN_PERMISSION_SETTINGS) == 0U) {
            ESP_LOGW(TAG, "ignoring %s override without settings permission",
                     record->manifest.id);
            continue;
        }
        if (!package_available(record)) {
            ESP_LOGW(TAG, "%s override unavailable; keeping built-in fallback",
                     record->manifest.id);
            continue;
        }
        registry_entry_t *entry = &s_entries[system_index];
        entry->package = record;
        entry->info.kind = APP_PLUGIN_PACKAGE;
    }

    size_t registry_count = SYSTEM_PLUGIN_COUNT;
    for (size_t index = 0; index < package_count; ++index) {
        plugin_record_t *record = &s_packages[index];
        if (record->manifest.kind != PLUGIN_KIND_APP) continue;
        if (system_index_for(record->manifest.id) >= 0) continue;
        registry_entry_t *entry = &s_entries[registry_count++];
        entry->info = (app_plugin_info_t) {
            .id = record->manifest.id,
            .name = record->manifest.name,
            .icon = LV_SYMBOL_FILE,
            .kind = APP_PLUGIN_PACKAGE,
            .pinned = false,
            .available = package_available(record),
        };
        entry->package = record;
    }
    s_count = registry_count;
}

size_t app_registry_count(void)
{
    return s_count;
}

size_t app_registry_pinned_count(void)
{
    return SYSTEM_PLUGIN_COUNT;
}

const app_plugin_info_t *app_registry_get(size_t index)
{
    return index < s_count ? &s_entries[index].info : NULL;
}

esp_err_t app_registry_enter(size_t index)
{
    if (s_active >= 0 || index >= s_count || !s_entries[index].info.available) {
        return ESP_ERR_INVALID_STATE;
    }
    registry_entry_t *entry = &s_entries[index];
    s_active_package = false;
    if (entry->package) {
        esp_err_t result = plugin_host_start(entry->package);
        if (result == ESP_OK) {
            s_active_package = true;
        } else if (entry->system) {
            ESP_LOGW(TAG, "%s override failed, using built-in fallback: %s",
                     entry->info.id, esp_err_to_name(result));
            entry->system->enter();
        } else {
            return result;
        }
    } else if (entry->system) {
        entry->system->enter();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
    s_active = (int)index;
    return ESP_OK;
}

void app_registry_exit(void)
{
    if (s_active < 0) return;
    registry_entry_t *entry = &s_entries[s_active];
    if (s_active_package) plugin_host_stop();
    else if (entry->system) entry->system->exit();
    s_active = -1;
    s_active_package = false;
}

void app_registry_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (s_active < 0) return;
    registry_entry_t *entry = &s_entries[s_active];

    if (s_active_package && plugin_host_handle_key(button, event)) return;

    if (button == BSP_BTN_OK && event == BSP_BTN_LONG) {
        if (!s_active_package && entry->system) {
            if (!entry->system->back || !entry->system->back()) s_home_requested = true;
            return;
        }
        plugin_vm_result_t result = plugin_host_dispatch(PLUGIN_EVENT_BACK);
        bool exit_requested = plugin_host_take_exit_request();
        if (result == PLUGIN_VM_NO_HANDLER || exit_requested) s_home_requested = true;
        return;
    }

    if (!s_active_package && entry->system) {
        entry->system->key(button, event);
        return;
    }

    plugin_event_t plugin_event;
    if (event == BSP_BTN_PRESS && button == BSP_BTN_UP) {
        plugin_event = PLUGIN_EVENT_UP;
    } else if (event == BSP_BTN_PRESS && button == BSP_BTN_DOWN) {
        plugin_event = PLUGIN_EVENT_DOWN;
    } else if (event == BSP_BTN_CLICK && button == BSP_BTN_OK) {
        plugin_event = PLUGIN_EVENT_OK;
    } else {
        return;
    }
    plugin_host_dispatch(plugin_event);
    if (plugin_host_take_exit_request()) s_home_requested = true;
}

bool app_registry_active(void)
{
    return s_active >= 0;
}

bool app_registry_take_home_request(void)
{
    bool requested = s_home_requested;
    s_home_requested = false;
    return requested;
}
