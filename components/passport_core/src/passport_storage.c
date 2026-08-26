#include "passport_storage.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "passport_storage";
static wl_handle_t s_wl = WL_INVALID_HANDLE;

esp_err_t passport_storage_ensure_dir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "创建目录失败 %s: errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t remove_tree_inner(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
    }

    DIR *dir = opendir(path);
    if (!dir) return ESP_FAIL;
    struct dirent *entry;
    char child[256];
    esp_err_t result = ESP_OK;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(child) || remove_tree_inner(child) != ESP_OK) {
            result = ESP_FAIL;
            break;
        }
    }
    closedir(dir);
    if (result == ESP_OK && rmdir(path) != 0) result = ESP_FAIL;
    return result;
}

esp_err_t passport_storage_remove_tree(const char *path)
{
    if (!path || strncmp(path, PASSPORT_FS_ROOT "/", strlen(PASSPORT_FS_ROOT) + 1) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return remove_tree_inner(path);
}

esp_err_t passport_storage_read_text(const char *path, char *buf, size_t capacity, size_t *out_len)
{
    if (!path || !buf || capacity < 2) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    size_t n = fread(buf, 1, capacity - 1, f);
    const bool overflow = !feof(f);
    fclose(f);
    if (overflow) return ESP_ERR_INVALID_SIZE;
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return ESP_OK;
}

esp_err_t passport_storage_init(void)
{
    if (s_wl != WL_INVALID_HANDLE) return ESP_OK;

    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(PASSPORT_FS_ROOT, "appfs", &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "挂载 appfs 失败: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = passport_storage_ensure_dir(PASSPORT_APPS_DIR)) != ESP_OK ||
        (err = passport_storage_ensure_dir(PASSPORT_THEMES_DIR)) != ESP_OK ||
        (err = passport_storage_ensure_dir(PASSPORT_STAGING_DIR)) != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "插件存储已挂载到 %s", PASSPORT_FS_ROOT);
    return ESP_OK;
}
