#include "passport_theme.h"

#include "passport_package.h"
#include "passport_storage.h"
#include "passport_theme_parser.h"
#include "esp_log.h"
#include "nvs.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "passport_theme";
static const passport_theme_tokens_t DEFAULT_THEME = {
    .background = 0xF5F2E8,
    .surface = 0xFFFFFF,
    .item_background = 0xF5F2E8,
    .text = 0x17202A,
    .muted_text = 0x66727A,
    .accent = 0x1677FF,
    .selection_text = 0xFFFFFF,
    .divider = 0xD8DCE0,
    .border = 0xD8DCE0,
    .shadow = 0x000000,
    .spacing = 6,
    .radius = 4,
    .border_width = 0,
    .shadow_width = 0,
    .shadow_spread = 0,
    .shadow_opacity = 0,
    .shadow_offset_x = 0,
    .shadow_offset_y = 0,
};

static passport_theme_tokens_t s_tokens;
static char s_current_id[PASSPORT_THEME_ID_MAX] = "default";

static esp_err_t load_theme_file(const char *id, passport_theme_tokens_t *out, char *name, size_t name_cap)
{
    if (!passport_package_id_is_valid(id) || !out) return ESP_ERR_INVALID_ARG;
    char path[220];
    int path_len = snprintf(path, sizeof(path), "%s/%s/manifest.json", PASSPORT_THEMES_DIR, id);
    if (path_len < 0 || (size_t)path_len >= sizeof(path)) return ESP_ERR_INVALID_SIZE;

    /* Theme loading can run in the 6 KiB system task. Allocate the bounded
     * JSON document temporarily instead of consuming most of that task stack. */
    char *json = malloc(PASSPORT_PACKAGE_MANIFEST_MAX + 1U);
    if (!json) return ESP_ERR_NO_MEM;
    size_t json_len = 0;
    esp_err_t err = passport_storage_read_text(
        path, json, PASSPORT_PACKAGE_MANIFEST_MAX + 1U, &json_len);
    if (err != ESP_OK) {
        free(json);
        return err;
    }

    passport_manifest_t manifest;
    passport_theme_tokens_t parsed;
    err = passport_theme_parse_manifest_json(json, json_len, &manifest, &parsed);
    free(json);
    if (err != ESP_OK || strcmp(manifest.id, id) != 0) return ESP_ERR_INVALID_ARG;
    if (name) {
        size_t name_len = strlen(manifest.name);
        if (name_cap == 0U || name_len >= name_cap) return ESP_ERR_INVALID_SIZE;
        memcpy(name, manifest.name, name_len + 1U);
    }
    *out = parsed;
    return err;
}

static void persist_current(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open("passport", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_str(nvs, "theme", id);
    nvs_commit(nvs);
    nvs_close(nvs);
}

esp_err_t passport_theme_init(void)
{
    s_tokens = DEFAULT_THEME;
    memcpy(s_current_id, "default", sizeof("default"));
    nvs_handle_t nvs;
    if (nvs_open("passport", NVS_READONLY, &nvs) == ESP_OK) {
        char id[PASSPORT_THEME_ID_MAX];
        size_t len = sizeof(id);
        if (nvs_get_str(nvs, "theme", id, &len) == ESP_OK && strcmp(id, "default") != 0) {
            passport_theme_tokens_t loaded;
            if (load_theme_file(id, &loaded, NULL, 0) == ESP_OK) {
                s_tokens = loaded;
                memcpy(s_current_id, id, strlen(id) + 1);
            }
        }
        nvs_close(nvs);
    }
    return ESP_OK;
}

const passport_theme_tokens_t *passport_theme_current(void)
{
    return &s_tokens;
}

const char *passport_theme_current_id(void)
{
    return s_current_id;
}

esp_err_t passport_theme_apply(const char *id)
{
    if (!passport_package_id_is_valid(id)) return ESP_ERR_INVALID_ARG;
    if (strcmp(id, "default") == 0) {
        s_tokens = DEFAULT_THEME;
        memcpy(s_current_id, "default", sizeof("default"));
        persist_current("default");
        return ESP_OK;
    }
    passport_theme_tokens_t loaded;
    esp_err_t err = load_theme_file(id, &loaded, NULL, 0);
    if (err != ESP_OK) return err;
    s_tokens = loaded;
    memcpy(s_current_id, id, strlen(id) + 1);
    persist_current(id);
    ESP_LOGI(TAG, "已应用主题 %s", id);
    return ESP_OK;
}

size_t passport_theme_list(passport_theme_info_t *out, size_t capacity)
{
    if (!out || capacity == 0) return 0;
    size_t count = 0;
    memcpy(out[count].id, "default", sizeof("default"));
    memcpy(out[count].name, "默认主题", sizeof("默认主题"));
    ++count;

    DIR *dir = opendir(PASSPORT_THEMES_DIR);
    if (!dir) return count;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < capacity) {
        if (entry->d_name[0] == '.' || !passport_package_id_is_valid(entry->d_name)) continue;
        passport_theme_tokens_t ignored;
        char name[PASSPORT_THEME_NAME_MAX];
        if (load_theme_file(entry->d_name, &ignored, name, sizeof(name)) == ESP_OK) {
            memcpy(out[count].id, entry->d_name, strlen(entry->d_name) + 1);
            memcpy(out[count].name, name, strlen(name) + 1);
            ++count;
        }
    }
    closedir(dir);
    return count;
}
