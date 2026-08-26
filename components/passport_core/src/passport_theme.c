#include "passport_theme.h"

#include "passport_package.h"
#include "passport_storage.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "passport_theme";
static const passport_theme_tokens_t DEFAULT_THEME = {
    .background = 0xF5F2E8,
    .surface = 0xFFFFFF,
    .text = 0x17202A,
    .muted_text = 0x66727A,
    .accent = 0x1677FF,
    .divider = 0xD8DCE0,
    .spacing = 6,
    .radius = 4,
};

static passport_theme_tokens_t s_tokens;
static char s_current_id[PASSPORT_THEME_ID_MAX] = "default";

static bool parse_color(cJSON *item, uint32_t *out)
{
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    const char *s = item->valuestring;
    if (s[0] == '#') ++s;
    if (strlen(s) != 6) return false;
    char *end = NULL;
    unsigned long value = strtoul(s, &end, 16);
    if (!end || *end != '\0' || value > 0xFFFFFFUL) return false;
    *out = (uint32_t)value;
    return true;
}

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
    err = passport_package_parse_manifest_json(
        json, json_len, PASSPORT_PACKAGE_THEME, &manifest);
    if (err != ESP_OK || strcmp(manifest.id, id) != 0) {
        free(json);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *doc = cJSON_ParseWithLength(json, json_len);
    if (!doc) {
        free(json);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *tokens = cJSON_GetObjectItemCaseSensitive(doc, "tokens");
    bool ok = cJSON_IsObject(tokens);
    if (!ok) {
        cJSON_Delete(doc);
        free(json);
        return ESP_ERR_INVALID_ARG;
    }

    passport_theme_tokens_t next = DEFAULT_THEME;
    cJSON *item;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "background"); if (item && !parse_color(item, &next.background)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "surface"); if (item && !parse_color(item, &next.surface)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "text"); if (item && !parse_color(item, &next.text)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "muted_text"); if (item && !parse_color(item, &next.muted_text)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "accent"); if (item && !parse_color(item, &next.accent)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "divider"); if (item && !parse_color(item, &next.divider)) ok = false;
    item = cJSON_GetObjectItemCaseSensitive(tokens, "spacing");
    if (item) {
        if (!cJSON_IsNumber(item) || item->valueint < 2 || item->valueint > 12) ok = false;
        else next.spacing = (uint8_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(tokens, "radius");
    if (item) {
        if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 12) ok = false;
        else next.radius = (uint8_t)item->valueint;
    }
    if (ok) *out = next;
    if (ok && name && name_cap) {
        size_t manifest_name_len = strlen(manifest.name);
        if (manifest_name_len >= name_cap) ok = false;
        else memcpy(name, manifest.name, manifest_name_len + 1);
    }
    cJSON_Delete(doc);
    free(json);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
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
