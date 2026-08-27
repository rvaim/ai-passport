#include "passport_app_storage.h"

#include "passport_app_storage_model.h"
#include "passport_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORAGE_QUEUE_DEPTH 4U
#define STORAGE_WORKER_STACK_BYTES 4096U
#define STORAGE_PATH_BUFFER 256U

static const char *TAG = "passport_app_storage";

typedef struct {
    passport_app_storage_operation_t operation;
    passport_package_kind_t package_kind;
    uint32_t request_id;
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    char path[PASSPORT_APP_STORAGE_PATH_MAX];
    passport_app_storage_completion_cb_t callback;
    void *user;
    passport_app_storage_completion_t *completion;
    size_t data_size;
    uint8_t data[];
} storage_job_t;

typedef struct {
    uint32_t used_bytes;
    uint16_t file_count;
} storage_usage_t;

static QueueHandle_t s_jobs;
static TaskHandle_t s_worker;

static passport_app_storage_error_t error_from_errno(int value)
{
    if (value == ENOENT) return PASSPORT_APP_STORAGE_NOT_FOUND;
    if (value == ENOSPC) return PASSPORT_APP_STORAGE_NO_SPACE;
    return PASSPORT_APP_STORAGE_IO_ERROR;
}

static bool build_path(char *out, size_t capacity, const char *format,
                       const char *app_id, const char *leaf)
{
    int length = leaf ? snprintf(out, capacity, format, app_id, leaf) :
                        snprintf(out, capacity, format, app_id);
    return length > 0 && (size_t)length < capacity;
}

static bool build_container_path(char *out, size_t capacity, const char *app_id)
{
    return build_path(out, capacity, PASSPORT_APPS_DIR "/%s", app_id, NULL);
}

static bool build_bundle_path(char *out, size_t capacity, const char *app_id)
{
    return build_path(out, capacity,
                      PASSPORT_APPS_DIR "/%s/" PASSPORT_APP_BUNDLE_NAME,
                      app_id, NULL);
}

static bool build_data_root(char *out, size_t capacity, const char *app_id)
{
    return build_path(out, capacity,
                      PASSPORT_APPS_DIR "/%s/" PASSPORT_APP_DATA_NAME,
                      app_id, NULL);
}

static bool build_data_path(char *out, size_t capacity, const char *app_id,
                            const char *relative)
{
    return build_path(out, capacity,
                      PASSPORT_APPS_DIR "/%s/" PASSPORT_APP_DATA_NAME "/%s",
                      app_id, relative);
}

static bool build_theme_path(char *out, size_t capacity, const char *theme_id)
{
    return build_path(out, capacity, PASSPORT_THEMES_DIR "/%s", theme_id, NULL);
}

static passport_app_storage_error_t ensure_data_root(const char *app_id,
                                                     char *root,
                                                     size_t capacity)
{
    char bundle[STORAGE_PATH_BUFFER];
    struct stat st;
    if (!build_bundle_path(bundle, sizeof(bundle), app_id) ||
        stat(bundle, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return PASSPORT_APP_STORAGE_CANCELED;
    }
    if (!build_data_root(root, capacity, app_id)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    if (stat(root, &st) == 0) {
        return S_ISDIR(st.st_mode) ? PASSPORT_APP_STORAGE_OK :
                                    PASSPORT_APP_STORAGE_IO_ERROR;
    }
    if (errno != ENOENT) return error_from_errno(errno);
    if (mkdir(root, 0755) != 0) return error_from_errno(errno);
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t ensure_parent_directories(
    const char *root, char *path)
{
    for (char *cursor = path + strlen(root) + 1U; *cursor; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            *cursor = '/';
            return error_from_errno(errno);
        }
        *cursor = '/';
    }
    return PASSPORT_APP_STORAGE_OK;
}

static bool has_suffix(const char *value, const char *suffix)
{
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length > suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

static bool build_shadow_path(char *out, size_t capacity, const char *target,
                              const char *suffix)
{
    const char *name = strrchr(target, '/');
    if (!name || !name[1]) return false;
    size_t parent_length = (size_t)(name - target);
    int length = snprintf(out, capacity, "%.*s/.%s%s", (int)parent_length,
                          target, name + 1, suffix);
    return length > 0 && (size_t)length < capacity;
}

static passport_app_storage_error_t recover_target(const char *target)
{
    char temporary[STORAGE_PATH_BUFFER];
    char backup[STORAGE_PATH_BUFFER];
    if (!build_shadow_path(temporary, sizeof(temporary), target, ".tmp") ||
        !build_shadow_path(backup, sizeof(backup), target, ".bak")) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }

    struct stat target_stat;
    struct stat backup_stat;
    bool target_exists = stat(target, &target_stat) == 0;
    bool backup_exists = stat(backup, &backup_stat) == 0;
    if (!target_exists && backup_exists) {
        if (rename(backup, target) != 0) return error_from_errno(errno);
    } else if (target_exists && backup_exists && unlink(backup) != 0) {
        return error_from_errno(errno);
    }
    if (unlink(temporary) != 0 && errno != ENOENT) return error_from_errno(errno);
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t recover_tree(const char *root)
{
    DIR *directory = opendir(root);
    if (!directory) return errno == ENOENT ? PASSPORT_APP_STORAGE_OK :
                                            error_from_errno(errno);
    struct dirent *entry;
    char child[STORAGE_PATH_BUFFER];
    passport_app_storage_error_t result = PASSPORT_APP_STORAGE_OK;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        int length = snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
        if (length <= 0 || (size_t)length >= sizeof(child)) {
            result = PASSPORT_APP_STORAGE_IO_ERROR;
            break;
        }
        struct stat st;
        int stat_result = stat(child, &st);
        if (stat_result != 0 && errno == ENOENT) continue;
        if (stat_result != 0) {
            result = error_from_errno(errno);
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            result = recover_tree(child);
            if (result != PASSPORT_APP_STORAGE_OK) break;
            continue;
        }
        if (entry->d_name[0] != '.' ||
            (!has_suffix(entry->d_name, ".tmp") &&
             !has_suffix(entry->d_name, ".bak"))) {
            continue;
        }
        const char *suffix = has_suffix(entry->d_name, ".tmp") ? ".tmp" : ".bak";
        size_t name_length = strlen(entry->d_name) - strlen(suffix) - 1U;
        char target[STORAGE_PATH_BUFFER];
        int target_length = snprintf(target, sizeof(target), "%s/%.*s", root,
                                     (int)name_length, entry->d_name + 1);
        if (target_length <= 0 || (size_t)target_length >= sizeof(target)) {
            result = PASSPORT_APP_STORAGE_IO_ERROR;
            break;
        }
        result = recover_target(target);
        if (result != PASSPORT_APP_STORAGE_OK) break;
    }
    closedir(directory);
    return result;
}

static passport_app_storage_error_t scan_usage(const char *root,
                                               storage_usage_t *usage)
{
    DIR *directory = opendir(root);
    if (!directory) return errno == ENOENT ? PASSPORT_APP_STORAGE_OK :
                                            error_from_errno(errno);
    struct dirent *entry;
    char child[STORAGE_PATH_BUFFER];
    passport_app_storage_error_t result = PASSPORT_APP_STORAGE_OK;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        int length = snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
        if (length <= 0 || (size_t)length >= sizeof(child)) {
            result = PASSPORT_APP_STORAGE_IO_ERROR;
            break;
        }
        struct stat st;
        if (stat(child, &st) != 0) {
            result = error_from_errno(errno);
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            result = scan_usage(child, usage);
        } else if (S_ISREG(st.st_mode)) {
            if (usage->file_count == UINT16_MAX) {
                result = PASSPORT_APP_STORAGE_QUOTA_EXCEEDED;
            } else {
                ++usage->file_count;
                uint32_t allocated = passport_app_storage_allocated_bytes(
                    st.st_size < 0 ? 0U : (size_t)st.st_size);
                if (UINT32_MAX - usage->used_bytes < allocated) {
                    result = PASSPORT_APP_STORAGE_QUOTA_EXCEEDED;
                } else {
                    usage->used_bytes += allocated;
                }
            }
        }
        if (result != PASSPORT_APP_STORAGE_OK) break;
    }
    closedir(directory);
    return result;
}

static passport_app_storage_error_t read_file(storage_job_t *job,
                                              passport_app_storage_completion_t *completion)
{
    char root[STORAGE_PATH_BUFFER];
    char path[STORAGE_PATH_BUFFER];
    if (!build_data_root(root, sizeof(root), job->app_id) ||
        !build_data_path(path, sizeof(path), job->app_id, job->path)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    passport_app_storage_error_t result = recover_tree(root);
    if (result != PASSPORT_APP_STORAGE_OK) return result;

    struct stat st;
    if (stat(path, &st) != 0) return error_from_errno(errno);
    if (!S_ISREG(st.st_mode)) return PASSPORT_APP_STORAGE_INVALID_PATH;
    if (st.st_size < 0 || (size_t)st.st_size > PASSPORT_APP_STORAGE_FILE_MAX) {
        return PASSPORT_APP_STORAGE_TOO_LARGE;
    }
    size_t size = (size_t)st.st_size;
    uint8_t *data = size ? malloc(size) : NULL;
    if (size && !data) return PASSPORT_APP_STORAGE_NO_MEMORY;
    FILE *file = fopen(path, "rb");
    if (!file || (size && fread(data, 1, size, file) != size)) {
        int saved_errno = errno;
        if (file) fclose(file);
        free(data);
        return error_from_errno(saved_errno);
    }
    if (fclose(file) != 0) {
        free(data);
        return PASSPORT_APP_STORAGE_IO_ERROR;
    }
    completion->data = data;
    completion->data_size = size;
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t write_file(storage_job_t *job)
{
    char root[STORAGE_PATH_BUFFER];
    passport_app_storage_error_t result = ensure_data_root(
        job->app_id, root, sizeof(root));
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    result = recover_tree(root);
    if (result != PASSPORT_APP_STORAGE_OK) return result;

    char path[STORAGE_PATH_BUFFER];
    if (!build_data_path(path, sizeof(path), job->app_id, job->path)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    result = ensure_parent_directories(root, path);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    result = recover_target(path);
    if (result != PASSPORT_APP_STORAGE_OK) return result;

    storage_usage_t usage = {0};
    result = scan_usage(root, &usage);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    struct stat old_stat;
    bool replacing = stat(path, &old_stat) == 0;
    if (replacing && !S_ISREG(old_stat.st_mode)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    uint32_t old_bytes = replacing ? passport_app_storage_allocated_bytes(
        old_stat.st_size < 0 ? 0U : (size_t)old_stat.st_size) : 0U;
    if (!passport_app_storage_quota_allows(
            usage.used_bytes, old_bytes, job->data_size,
            usage.file_count, replacing)) {
        return PASSPORT_APP_STORAGE_QUOTA_EXCEEDED;
    }
    uint32_t total_used = 0U;
    uint32_t replacement_bytes = passport_app_storage_allocated_bytes(job->data_size);
    if (passport_app_storage_total_usage(&total_used) != ESP_OK ||
        old_bytes > total_used ||
        replacement_bytes > PASSPORT_APP_STORAGE_GLOBAL_QUOTA_BYTES ||
        total_used - old_bytes >
            PASSPORT_APP_STORAGE_GLOBAL_QUOTA_BYTES - replacement_bytes) {
        return PASSPORT_APP_STORAGE_QUOTA_EXCEEDED;
    }

    char temporary[STORAGE_PATH_BUFFER];
    char backup[STORAGE_PATH_BUFFER];
    if (!build_shadow_path(temporary, sizeof(temporary), path, ".tmp") ||
        !build_shadow_path(backup, sizeof(backup), path, ".bak")) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) return error_from_errno(errno);
    bool write_ok = (!job->data_size ||
                     fwrite(job->data, 1, job->data_size, file) == job->data_size) &&
                    fflush(file) == 0 && fsync(fileno(file)) == 0;
    int close_result = fclose(file);
    if (!write_ok || close_result != 0) {
        int saved_errno = errno;
        unlink(temporary);
        return error_from_errno(saved_errno);
    }

    if (replacing && rename(path, backup) != 0) {
        int saved_errno = errno;
        unlink(temporary);
        return error_from_errno(saved_errno);
    }
    if (rename(temporary, path) != 0) {
        int saved_errno = errno;
        if (replacing) rename(backup, path);
        unlink(temporary);
        return error_from_errno(saved_errno);
    }
    if (replacing && unlink(backup) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "旧数据备份稍后清理: %s", backup);
    }
    return PASSPORT_APP_STORAGE_OK;
}

static void remove_empty_parents(const char *root, char *path)
{
    size_t root_length = strlen(root);
    char *slash = strrchr(path, '/');
    while (slash && (size_t)(slash - path) > root_length) {
        *slash = '\0';
        if (rmdir(path) != 0) break;
        slash = strrchr(path, '/');
    }
}

static passport_app_storage_error_t remove_path(storage_job_t *job)
{
    char root[STORAGE_PATH_BUFFER];
    char path[STORAGE_PATH_BUFFER];
    if (!build_data_root(root, sizeof(root), job->app_id) ||
        !build_data_path(path, sizeof(path), job->app_id, job->path)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    passport_app_storage_error_t result = recover_tree(root);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    struct stat st;
    if (stat(path, &st) != 0) return error_from_errno(errno);
    esp_err_t remove_result = S_ISDIR(st.st_mode) ?
        passport_storage_remove_tree(path) :
        (unlink(path) == 0 ? ESP_OK : ESP_FAIL);
    if (remove_result != ESP_OK) return error_from_errno(errno);
    remove_empty_parents(root, path);
    return PASSPORT_APP_STORAGE_OK;
}

static int compare_entries(const void *left, const void *right)
{
    const passport_app_storage_entry_t *a = left;
    const passport_app_storage_entry_t *b = right;
    return strcmp(a->name, b->name);
}

static passport_app_storage_error_t list_directory(
    storage_job_t *job, passport_app_storage_completion_t *completion)
{
    char root[STORAGE_PATH_BUFFER];
    if (!build_data_root(root, sizeof(root), job->app_id)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    passport_app_storage_error_t result = recover_tree(root);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    char path[STORAGE_PATH_BUFFER];
    if (job->path[0]) {
        if (!build_data_path(path, sizeof(path), job->app_id, job->path)) {
            return PASSPORT_APP_STORAGE_INVALID_PATH;
        }
    } else {
        memcpy(path, root, strlen(root) + 1U);
    }

    DIR *directory = opendir(path);
    if (!directory) {
        if (errno == ENOENT && !job->path[0]) return PASSPORT_APP_STORAGE_OK;
        return error_from_errno(errno);
    }
    passport_app_storage_entry_t *entries = calloc(
        PASSPORT_APP_STORAGE_FILE_COUNT_MAX, sizeof(*entries));
    if (!entries) {
        closedir(directory);
        return PASSPORT_APP_STORAGE_NO_MEMORY;
    }
    struct dirent *entry;
    size_t count = 0U;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (count >= PASSPORT_APP_STORAGE_FILE_COUNT_MAX) {
            result = PASSPORT_APP_STORAGE_TOO_LARGE;
            break;
        }
        char child[STORAGE_PATH_BUFFER];
        int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat st;
        if (length <= 0 || (size_t)length >= sizeof(child) || stat(child, &st) != 0) {
            result = PASSPORT_APP_STORAGE_IO_ERROR;
            break;
        }
        size_t name_length = strnlen(entry->d_name, sizeof(entries[count].name));
        if (name_length >= sizeof(entries[count].name)) {
            result = PASSPORT_APP_STORAGE_INVALID_PATH;
            break;
        }
        memcpy(entries[count].name, entry->d_name, name_length + 1U);
        entries[count].is_directory = S_ISDIR(st.st_mode);
        entries[count].size = S_ISREG(st.st_mode) && st.st_size > 0 ?
                              (uint32_t)st.st_size : 0U;
        ++count;
    }
    closedir(directory);
    if (result != PASSPORT_APP_STORAGE_OK) {
        free(entries);
        return result;
    }
    qsort(entries, count, sizeof(*entries), compare_entries);
    completion->entries = entries;
    completion->entry_count = count;
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t read_usage(
    storage_job_t *job, passport_app_storage_completion_t *completion)
{
    char root[STORAGE_PATH_BUFFER];
    if (!build_data_root(root, sizeof(root), job->app_id)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    passport_app_storage_error_t result = recover_tree(root);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    storage_usage_t usage = {0};
    result = scan_usage(root, &usage);
    if (result != PASSPORT_APP_STORAGE_OK) return result;
    completion->used_bytes = usage.used_bytes;
    completion->quota_bytes = PASSPORT_APP_STORAGE_QUOTA_BYTES;
    completion->file_count = usage.file_count;
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t move_to_trash(const char *target,
                                                   const char *kind,
                                                   const char *label,
                                                   const char *id,
                                                   uint32_t request_id)
{
    char trash[STORAGE_PATH_BUFFER];
    int length = snprintf(trash, sizeof(trash), PASSPORT_TRASH_DIR "/%s-%s-%08lx",
                          kind, id, (unsigned long)request_id);
    if (length <= 0 || (size_t)length >= sizeof(trash)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    if (passport_storage_remove_tree(trash) != ESP_OK) {
        return PASSPORT_APP_STORAGE_IO_ERROR;
    }
    if (rename(target, trash) != 0) return error_from_errno(errno);
    if (passport_storage_remove_tree(trash) != ESP_OK) {
        ESP_LOGW(TAG, "%s已卸载，残留目录将在重启时清理: %s", label, trash);
    }
    return PASSPORT_APP_STORAGE_OK;
}

static passport_app_storage_error_t uninstall_app(storage_job_t *job)
{
    char container[STORAGE_PATH_BUFFER];
    if (!build_container_path(container, sizeof(container), job->app_id)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    struct stat st;
    if (stat(container, &st) != 0) return error_from_errno(errno);
    return move_to_trash(container, "app", "插件", job->app_id, job->request_id);
}

static passport_app_storage_error_t uninstall_theme(storage_job_t *job)
{
    if (strcmp(job->app_id, "default") == 0) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }

    char theme[STORAGE_PATH_BUFFER];
    if (!build_theme_path(theme, sizeof(theme), job->app_id)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    struct stat st;
    if (stat(theme, &st) != 0) return error_from_errno(errno);
    if (!S_ISDIR(st.st_mode)) return PASSPORT_APP_STORAGE_INVALID_PATH;

    return move_to_trash(theme, "theme", "主题", job->app_id, job->request_id);
}

static void execute_job(storage_job_t *job,
                        passport_app_storage_completion_t *completion)
{
    if (!passport_storage_lock(UINT32_MAX)) {
        completion->error = PASSPORT_APP_STORAGE_IO_ERROR;
        return;
    }
    switch (job->operation) {
    case PASSPORT_APP_STORAGE_READ:
        completion->error = read_file(job, completion);
        break;
    case PASSPORT_APP_STORAGE_WRITE:
        completion->error = write_file(job);
        break;
    case PASSPORT_APP_STORAGE_REMOVE:
        completion->error = remove_path(job);
        break;
    case PASSPORT_APP_STORAGE_LIST:
        completion->error = list_directory(job, completion);
        break;
    case PASSPORT_APP_STORAGE_USAGE:
        completion->error = read_usage(job, completion);
        break;
    case PASSPORT_APP_STORAGE_UNINSTALL:
        completion->error = job->package_kind == PASSPORT_PACKAGE_APP ?
                            uninstall_app(job) : uninstall_theme(job);
        break;
    default:
        completion->error = PASSPORT_APP_STORAGE_IO_ERROR;
        break;
    }
    passport_storage_unlock();
}

static void storage_worker(void *argument)
{
    (void)argument;
    storage_job_t *job = NULL;
    while (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
        passport_app_storage_completion_t *completion = job->completion;
        completion->operation = job->operation;
        completion->package_kind = job->package_kind;
        completion->request_id = job->request_id;
        memcpy(completion->app_id, job->app_id, strlen(job->app_id) + 1U);
        execute_job(job, completion);
        passport_app_storage_completion_cb_t callback = job->callback;
        void *user = job->user;
        free(job);
        callback(completion, user);
    }
}

esp_err_t passport_app_storage_init(void)
{
    if (s_worker) return ESP_OK;
    if (passport_storage_remove_tree(PASSPORT_TRASH_DIR) != ESP_OK ||
        passport_storage_ensure_dir(PASSPORT_TRASH_DIR) != ESP_OK) {
        return ESP_FAIL;
    }
    s_jobs = xQueueCreate(STORAGE_QUEUE_DEPTH, sizeof(storage_job_t *));
    if (!s_jobs) return ESP_ERR_NO_MEM;
    if (xTaskCreate(storage_worker, "pap_storage", STORAGE_WORKER_STACK_BYTES,
                    NULL, 4, &s_worker) != pdPASS) {
        vQueueDelete(s_jobs);
        s_jobs = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static passport_app_storage_error_t submit_job(
    passport_app_storage_operation_t operation,
    passport_package_kind_t package_kind, const char *app_id,
    const char *path, const void *data, size_t data_size, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    if (!s_jobs || !passport_package_id_is_valid(app_id) || !callback ||
        request_id == 0U) {
        return PASSPORT_APP_STORAGE_CANCELED;
    }
    const bool has_path = operation >= PASSPORT_APP_STORAGE_READ &&
                          operation <= PASSPORT_APP_STORAGE_LIST;
    const bool allow_empty = operation == PASSPORT_APP_STORAGE_LIST;
    const char *checked_path = path ? path : "";
    if ((has_path &&
         !passport_app_storage_path_is_safe(checked_path, allow_empty)) ||
        data_size > PASSPORT_APP_STORAGE_FILE_MAX) {
        return data_size > PASSPORT_APP_STORAGE_FILE_MAX ?
               PASSPORT_APP_STORAGE_TOO_LARGE :
               PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    storage_job_t *job = calloc(1, sizeof(*job) + data_size);
    if (!job) return PASSPORT_APP_STORAGE_NO_MEMORY;
    passport_app_storage_completion_t *completion = calloc(1, sizeof(*completion));
    if (!completion) {
        free(job);
        return PASSPORT_APP_STORAGE_NO_MEMORY;
    }
    job->operation = operation;
    job->package_kind = package_kind;
    job->request_id = request_id;
    memcpy(job->app_id, app_id, strlen(app_id) + 1U);
    if (checked_path[0]) memcpy(job->path, checked_path, strlen(checked_path) + 1U);
    job->callback = callback;
    job->user = user;
    job->completion = completion;
    job->data_size = data_size;
    if (data_size) memcpy(job->data, data, data_size);
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        free(completion);
        free(job);
        return PASSPORT_APP_STORAGE_BUSY;
    }
    return PASSPORT_APP_STORAGE_OK;
}

passport_app_storage_error_t passport_app_storage_read_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_job(PASSPORT_APP_STORAGE_READ, PASSPORT_PACKAGE_APP,
                      app_id, path, NULL, 0,
                      request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_write_async(
    const char *app_id, const char *path, const void *data, size_t size,
    uint32_t request_id, passport_app_storage_completion_cb_t callback,
    void *user)
{
    if (size && !data) return PASSPORT_APP_STORAGE_IO_ERROR;
    return submit_job(PASSPORT_APP_STORAGE_WRITE, PASSPORT_PACKAGE_APP,
                      app_id, path, data, size,
                      request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_remove_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_job(PASSPORT_APP_STORAGE_REMOVE, PASSPORT_PACKAGE_APP,
                      app_id, path, NULL, 0,
                      request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_list_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_job(PASSPORT_APP_STORAGE_LIST, PASSPORT_PACKAGE_APP,
                      app_id, path, NULL, 0,
                      request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_usage_async(
    const char *app_id, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_job(PASSPORT_APP_STORAGE_USAGE, PASSPORT_PACKAGE_APP,
                      app_id, "", NULL, 0,
                      request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_uninstall_async(
    passport_package_kind_t kind, const char *package_id, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    if ((kind != PASSPORT_PACKAGE_APP && kind != PASSPORT_PACKAGE_THEME) ||
        (kind == PASSPORT_PACKAGE_THEME && package_id &&
         strcmp(package_id, "default") == 0)) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    return submit_job(PASSPORT_APP_STORAGE_UNINSTALL, kind,
                      package_id, "", NULL, 0,
                      request_id, callback, user);
}

void passport_app_storage_completion_free(
    passport_app_storage_completion_t *completion)
{
    if (!completion) return;
    free(completion->data);
    free(completion->entries);
    free(completion);
}

esp_err_t passport_app_storage_total_usage(uint32_t *out_used_bytes)
{
    if (!out_used_bytes) return ESP_ERR_INVALID_ARG;
    if (!passport_storage_lock(UINT32_MAX)) return ESP_ERR_INVALID_STATE;
    uint32_t total = 0U;
    DIR *apps = opendir(PASSPORT_APPS_DIR);
    if (!apps) {
        passport_storage_unlock();
        if (errno == ENOENT) {
            *out_used_bytes = 0U;
            return ESP_OK;
        }
        return ESP_FAIL;
    }
    struct dirent *entry;
    esp_err_t result = ESP_OK;
    while ((entry = readdir(apps)) != NULL) {
        if (entry->d_name[0] == '.' ||
            !passport_package_id_is_valid(entry->d_name)) continue;
        char root[STORAGE_PATH_BUFFER];
        if (!build_data_root(root, sizeof(root), entry->d_name)) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        passport_app_storage_error_t storage_error = recover_tree(root);
        storage_usage_t usage = {0};
        if (storage_error == PASSPORT_APP_STORAGE_OK) {
            storage_error = scan_usage(root, &usage);
        }
        if (storage_error != PASSPORT_APP_STORAGE_OK ||
            UINT32_MAX - total < usage.used_bytes) {
            result = ESP_FAIL;
            break;
        }
        total += usage.used_bytes;
    }
    closedir(apps);
    passport_storage_unlock();
    if (result == ESP_OK) *out_used_bytes = total;
    return result;
}
