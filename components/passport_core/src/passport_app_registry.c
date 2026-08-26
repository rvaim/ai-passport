#include "passport_app_registry.h"

#include "passport_storage.h"
#include "esp_log.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "passport_registry";
static passport_app_info_t s_apps[PASSPORT_MAX_INSTALLED_APPS];
static size_t s_count;

static esp_err_t parse_installed_manifest(const char *root, passport_manifest_t *out)
{
    char path[220];
    char json[4096];
    int path_len = snprintf(path, sizeof(path), "%s/manifest.json", root);
    if (path_len < 0 || (size_t)path_len >= sizeof(path)) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = passport_storage_read_text(path, json, sizeof(json), NULL);
    if (err != ESP_OK) return err;
    return passport_package_parse_manifest_json(json, strlen(json), PASSPORT_PACKAGE_APP, out);
}

static bool build_app_root(char *out, size_t capacity, const char *id)
{
    if (!passport_package_id_is_valid(id)) return false;
    const size_t root_len = strlen(PASSPORT_APPS_DIR);
    const size_t id_len = strlen(id);
    if (root_len >= capacity || id_len >= capacity - root_len - 1) return false;
    memcpy(out, PASSPORT_APPS_DIR, root_len);
    out[root_len] = '/';
    memcpy(out + root_len + 1, id, id_len + 1);
    return true;
}

esp_err_t passport_app_registry_scan(void)
{
    s_count = 0;
    DIR *dir = opendir(PASSPORT_APPS_DIR);
    if (!dir) return ESP_ERR_NOT_FOUND;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_count < PASSPORT_MAX_INSTALLED_APPS) {
        if (entry->d_name[0] == '.') continue;
        passport_app_info_t info = {0};
        if (build_app_root(info.root, sizeof(info.root), entry->d_name) &&
            parse_installed_manifest(info.root, &info.manifest) == ESP_OK &&
            strcmp(info.manifest.id, entry->d_name) == 0) {
            s_apps[s_count++] = info;
        } else {
            ESP_LOGW(TAG, "忽略无效插件目录: %s", entry->d_name);
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "发现 %u 个插件", (unsigned)s_count);
    return ESP_OK;
}

size_t passport_app_registry_count(void)
{
    return s_count;
}

const passport_app_info_t *passport_app_registry_get(size_t index)
{
    return index < s_count ? &s_apps[index] : NULL;
}
