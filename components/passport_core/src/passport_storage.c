#include "passport_storage.h"

#include "passport_app_storage.h"
#include "passport_package.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wear_levelling.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "passport_storage";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static SemaphoreHandle_t s_fs_mutex;

static bool partition_is_blank(const char *label)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition) return false;
    const size_t block_size = 4096U;
    uint8_t *block = malloc(block_size);
    if (!block) return false;
    bool blank = true;
    for (size_t offset = 0; offset < partition->size; offset += block_size) {
        size_t length = partition->size - offset;
        if (length > block_size) length = block_size;
        if (esp_partition_read(partition, offset, block, length) != ESP_OK) {
            blank = false;
            break;
        }
        for (size_t i = 0; i < length; ++i) {
            if (block[i] != 0xFFU) {
                blank = false;
                break;
            }
        }
        if (!blank) break;
    }
    free(block);
    return blank;
}

bool passport_storage_lock(uint32_t timeout_ms)
{
    if (!s_fs_mutex) return false;
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY :
                       pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_fs_mutex, ticks) == pdTRUE;
}

void passport_storage_unlock(void)
{
    if (s_fs_mutex) xSemaphoreGiveRecursive(s_fs_mutex);
}

esp_err_t passport_storage_ensure_dir(const char *path)
{
    if (!path || !passport_storage_lock(UINT32_MAX)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_OK;
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "创建目录失败 %s: errno=%d", path, errno);
        result = ESP_FAIL;
    }
    passport_storage_unlock();
    return result;
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
    if (!passport_storage_lock(UINT32_MAX)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = remove_tree_inner(path);
    passport_storage_unlock();
    return result;
}

esp_err_t passport_storage_read_text(const char *path, char *buf, size_t capacity, size_t *out_len)
{
    if (!path || !buf || capacity < 2) return ESP_ERR_INVALID_ARG;
    if (!passport_storage_lock(UINT32_MAX)) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(path, "rb");
    if (!f) {
        passport_storage_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(buf, 1, capacity - 1, f);
    const bool overflow = !feof(f);
    fclose(f);
    if (overflow) {
        passport_storage_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    buf[n] = '\0';
    if (out_len) *out_len = n;
    passport_storage_unlock();
    return ESP_OK;
}

esp_err_t passport_storage_init(void)
{
    if (s_wl != WL_INVALID_HANDLE) return passport_app_storage_init();

    if (!s_fs_mutex) {
        s_fs_mutex = xSemaphoreCreateRecursiveMutex();
        if (!s_fs_mutex) return ESP_ERR_NO_MEM;
    }

    const bool blank_partition = partition_is_blank("appfs");
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = blank_partition,
        .max_files = 8,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };
    wl_handle_t mounted_wl = WL_INVALID_HANDLE;
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        PASSPORT_FS_ROOT, "appfs", &cfg, &mounted_wl);
    if (err != ESP_OK) {
        if (mounted_wl != WL_INVALID_HANDLE) wl_unmount(mounted_wl);
        ESP_LOGE(TAG, "挂载 appfs 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_wl = mounted_wl;
    if (blank_partition) ESP_LOGI(TAG, "首次使用，已初始化空白 appfs");
    if ((err = passport_storage_ensure_dir(PASSPORT_APPS_DIR)) != ESP_OK ||
        (err = passport_storage_ensure_dir(PASSPORT_THEMES_DIR)) != ESP_OK ||
        (err = passport_storage_ensure_dir(PASSPORT_STAGING_DIR)) != ESP_OK ||
        (err = passport_storage_ensure_dir(PASSPORT_TRASH_DIR)) != ESP_OK) {
        return err;
    }
    err = passport_package_recover_installations();
    if (err != ESP_OK) return err;
    err = passport_app_storage_init();
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "插件存储已挂载到 %s", PASSPORT_FS_ROOT);
    return ESP_OK;
}
