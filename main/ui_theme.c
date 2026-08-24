#include "ui_theme.h"

#include "plugin_format.h"
#include "plugin_store.h"

#include "esp_log.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define THEME_NAMESPACE "pass_ui_v4"
#define ACTIVE_THEME_KEY "theme"
#define BUILTIN_THEME_ID "builtin.pixel"
#define UI_THEME_CAPACITY (PLUGIN_STORE_MAX_ACTIVE + 1U)

typedef struct {
    char id[PLUGIN_ID_SIZE];
    char name[PLUGIN_NAME_SIZE];
    plugin_record_t record;
    plugin_theme_descriptor_t descriptor;
    bool builtin;
} theme_entry_t;

static const plugin_theme_descriptor_t BUILTIN_THEME = {
    .colors = {
        [PLUGIN_THEME_COLOR_BACKGROUND] = 0x1689E8,
        [PLUGIN_THEME_COLOR_SURFACE] = 0xF4F4EA,
        [PLUGIN_THEME_COLOR_TEXT] = 0x17202A,
        [PLUGIN_THEME_COLOR_TEXT_MUTED] = 0x6F7F87,
        [PLUGIN_THEME_COLOR_ACCENT] = 0x82BE2D,
        [PLUGIN_THEME_COLOR_ACCENT_STRONG] = 0x55951D,
        [PLUGIN_THEME_COLOR_SELECTION] = 0xFFD928,
        [PLUGIN_THEME_COLOR_MUTED_SURFACE] = 0xD9E7EC,
        [PLUGIN_THEME_COLOR_DANGER] = 0xC72F27,
        [PLUGIN_THEME_COLOR_SUCCESS] = 0x55951D,
        [PLUGIN_THEME_COLOR_BORDER] = 0x17202A,
        [PLUGIN_THEME_COLOR_SELECTION_BORDER] = 0xFFFFFF,
    },
    .panel_radius = 0U,
    .panel_border_width = 4U,
    .panel_shadow_width = 1U,
    .panel_shadow_offset_x = 5,
    .panel_shadow_offset_y = 6,
    .decoration = PLUGIN_THEME_DECORATION_PIXEL_GROUND,
};

static const char *TAG = "ui_theme";
static theme_entry_t s_entries[UI_THEME_CAPACITY];
static size_t s_count;
static size_t s_active;
static uint32_t s_generation;
static bool s_initialized;

static void builtin_entry(theme_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->id, sizeof(entry->id), "%s", BUILTIN_THEME_ID);
    snprintf(entry->name, sizeof(entry->name), "%s", "像素原野");
    entry->descriptor = BUILTIN_THEME;
    entry->builtin = true;
}

static bool load_theme(const plugin_record_t *record, theme_entry_t *entry)
{
    plugin_image_t image;
    if (!record || record->manifest.kind != PLUGIN_KIND_THEME ||
        plugin_store_open(record, &image) != ESP_OK) {
        return false;
    }
    plugin_theme_descriptor_t descriptor;
    plugin_theme_result_t result = plugin_theme_parse(
        image.content + PLUGIN_MANIFEST_SIZE, image.record.manifest.code_size,
        &descriptor);
    plugin_store_close(&image);
    if (result != PLUGIN_THEME_OK) {
        ESP_LOGW(TAG, "ignoring invalid theme %s", record->manifest.id);
        return false;
    }
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->id, sizeof(entry->id), "%s", record->manifest.id);
    snprintf(entry->name, sizeof(entry->name), "%s", record->manifest.name);
    entry->record = *record;
    entry->descriptor = descriptor;
    return true;
}

static void sort_downloaded(void)
{
    for (size_t index = 2U; index < s_count; ++index) {
        theme_entry_t value = s_entries[index];
        size_t cursor = index;
        while (cursor > 1U &&
               strcmp(s_entries[cursor - 1U].name, value.name) > 0) {
            s_entries[cursor] = s_entries[cursor - 1U];
            --cursor;
        }
        s_entries[cursor] = value;
    }
}

static void persist_active(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(THEME_NAMESPACE, NVS_READWRITE, &handle);
    bool opened = result == ESP_OK;
    if (result == ESP_OK) result = nvs_set_str(handle, ACTIVE_THEME_KEY, s_entries[s_active].id);
    if (result == ESP_OK) result = nvs_commit(handle);
    if (opened) nvs_close(handle);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "cannot persist active theme: %s", esp_err_to_name(result));
    }
}

static void load_saved_id(char id[PLUGIN_ID_SIZE])
{
    nvs_handle_t handle;
    size_t length = PLUGIN_ID_SIZE;
    id[0] = '\0';
    if (nvs_open(THEME_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    if (nvs_get_str(handle, ACTIVE_THEME_KEY, id, &length) != ESP_OK) id[0] = '\0';
    nvs_close(handle);
}

bool ui_theme_refresh(void)
{
    char active_id[PLUGIN_ID_SIZE];
    plugin_theme_descriptor_t previous = s_count > 0U
        ? s_entries[s_active].descriptor : BUILTIN_THEME;
    if (s_count > 0U) snprintf(active_id, sizeof(active_id), "%s", s_entries[s_active].id);
    else load_saved_id(active_id);

    plugin_record_t records[PLUGIN_STORE_MAX_ACTIVE];
    size_t record_count = plugin_store_list(records, PLUGIN_STORE_MAX_ACTIVE);
    if (record_count > PLUGIN_STORE_MAX_ACTIVE) record_count = PLUGIN_STORE_MAX_ACTIVE;

    builtin_entry(&s_entries[0]);
    s_count = 1U;
    for (size_t index = 0; index < record_count && s_count < UI_THEME_CAPACITY; ++index) {
        if (records[index].manifest.kind != PLUGIN_KIND_THEME) continue;
        if (load_theme(&records[index], &s_entries[s_count])) ++s_count;
    }
    sort_downloaded();
    s_active = 0U;
    for (size_t index = 0; index < s_count; ++index) {
        if (strcmp(s_entries[index].id, active_id) == 0) {
            s_active = index;
            break;
        }
    }
    bool changed = strcmp(active_id, s_entries[s_active].id) != 0 ||
                   memcmp(&previous, &s_entries[s_active].descriptor,
                          sizeof(previous)) != 0;
    if (changed) ++s_generation;
    if (active_id[0] != '\0' && s_active == 0U &&
        strcmp(active_id, BUILTIN_THEME_ID) != 0) {
        persist_active();
    }
    ESP_LOGI(TAG, "%u themes, active=%s", (unsigned)s_count,
             s_entries[s_active].id);
    return changed;
}

esp_err_t ui_theme_init(void)
{
    if (s_initialized) return ESP_OK;
    s_generation = 1U;
    ui_theme_refresh();
    s_initialized = true;
    return ESP_OK;
}

size_t ui_theme_count(void)
{
    return s_count;
}

size_t ui_theme_active_index(void)
{
    return s_active;
}

const char *ui_theme_name(size_t index)
{
    return index < s_count ? s_entries[index].name : "未知主题";
}

const char *ui_theme_active_name(void)
{
    return ui_theme_name(s_active);
}

bool ui_theme_is_active(const char *plugin_id)
{
    return plugin_id && s_count > 0U && strcmp(s_entries[s_active].id, plugin_id) == 0;
}

esp_err_t ui_theme_select(size_t index)
{
    if (!s_initialized || index >= s_count) return ESP_ERR_INVALID_ARG;
    if (index == s_active) return ESP_OK;
    s_active = index;
    ++s_generation;
    persist_active();
    ESP_LOGI(TAG, "selected %s", s_entries[s_active].id);
    return ESP_OK;
}

esp_err_t ui_theme_select_next(int32_t *index)
{
    if (!index || s_count == 0U) return ESP_ERR_INVALID_ARG;
    size_t next = (s_active + 1U) % s_count;
    esp_err_t result = ui_theme_select(next);
    if (result == ESP_OK) *index = (int32_t)s_active;
    return result;
}

uint32_t ui_theme_generation(void)
{
    return s_generation;
}

uint32_t ui_theme_color(plugin_theme_color_t token)
{
    if (token >= PLUGIN_THEME_COLOR_COUNT || s_count == 0U) {
        return BUILTIN_THEME.colors[PLUGIN_THEME_COLOR_TEXT];
    }
    return s_entries[s_active].descriptor.colors[token];
}

const plugin_theme_descriptor_t *ui_theme_descriptor(void)
{
    return s_count > 0U ? &s_entries[s_active].descriptor : &BUILTIN_THEME;
}
