#pragma once

#include "esp_err.h"
#include "passport_package.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_APP_STORAGE_PATH_MAX 96U
#define PASSPORT_APP_STORAGE_FILE_MAX 4096U
#define PASSPORT_APP_STORAGE_FILE_COUNT_MAX 16U
#define PASSPORT_APP_STORAGE_QUOTA_BYTES (64U * 1024U)
#define PASSPORT_APP_STORAGE_GLOBAL_QUOTA_BYTES (1024U * 1024U)

typedef enum {
    PASSPORT_APP_STORAGE_OK = 0,
    PASSPORT_APP_STORAGE_NOT_FOUND,
    PASSPORT_APP_STORAGE_INVALID_PATH,
    PASSPORT_APP_STORAGE_TOO_LARGE,
    PASSPORT_APP_STORAGE_QUOTA_EXCEEDED,
    PASSPORT_APP_STORAGE_NO_SPACE,
    PASSPORT_APP_STORAGE_BUSY,
    PASSPORT_APP_STORAGE_IO_ERROR,
    PASSPORT_APP_STORAGE_CANCELED,
    PASSPORT_APP_STORAGE_NO_MEMORY,
} passport_app_storage_error_t;

typedef enum {
    PASSPORT_APP_STORAGE_READ = 1,
    PASSPORT_APP_STORAGE_WRITE,
    PASSPORT_APP_STORAGE_REMOVE,
    PASSPORT_APP_STORAGE_LIST,
    PASSPORT_APP_STORAGE_USAGE,
    PASSPORT_APP_STORAGE_UNINSTALL,
} passport_app_storage_operation_t;

typedef struct {
    char name[PASSPORT_APP_STORAGE_PATH_MAX];
    uint32_t size;
    bool is_directory;
} passport_app_storage_entry_t;

typedef struct passport_app_storage_completion {
    passport_app_storage_operation_t operation;
    passport_package_kind_t package_kind;
    passport_app_storage_error_t error;
    uint32_t request_id;
    /** Running app ID, or the package ID for a system uninstall completion. */
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    uint8_t *data;
    size_t data_size;
    passport_app_storage_entry_t *entries;
    size_t entry_count;
    uint32_t used_bytes;
    uint32_t quota_bytes;
    uint16_t file_count;
} passport_app_storage_completion_t;

/** Completion ownership transfers to the callback. */
typedef void (*passport_app_storage_completion_cb_t)(
    passport_app_storage_completion_t *completion, void *user);

/** Start the shared app-data and package-lifecycle I/O worker after appfs is mounted. */
esp_err_t passport_app_storage_init(void);

passport_app_storage_error_t passport_app_storage_read_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user);
passport_app_storage_error_t passport_app_storage_write_async(
    const char *app_id, const char *path, const void *data, size_t size,
    uint32_t request_id, passport_app_storage_completion_cb_t callback,
    void *user);
passport_app_storage_error_t passport_app_storage_remove_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user);
passport_app_storage_error_t passport_app_storage_list_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user);
passport_app_storage_error_t passport_app_storage_usage_async(
    const char *app_id, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user);

/** Queue atomic removal of an app (including data) or an installed theme. */
passport_app_storage_error_t passport_app_storage_uninstall_async(
    passport_package_kind_t kind, const char *package_id, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user);

void passport_app_storage_completion_free(
    passport_app_storage_completion_t *completion);

/** System-only stable allocation total used to reserve capacity during installs. */
esp_err_t passport_app_storage_total_usage(uint32_t *out_used_bytes);
