#include "passport_app_storage_model.h"

#include <string.h>

static bool portable_path_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-' || c == '/';
}

bool passport_app_storage_path_is_safe(const char *path, bool allow_empty)
{
    if (!path) return false;
    size_t path_length = strlen(path);
    if (path_length == 0U) return allow_empty;
    if (path_length >= PASSPORT_APP_STORAGE_PATH_MAX || path[0] == '/' ||
        path[path_length - 1U] == '/' || strchr(path, '\\')) {
        return false;
    }

    size_t depth = 0U;
    const char *segment = path;
    for (const unsigned char *p = (const unsigned char *)path; ; ++p) {
        if (*p != '\0' && !portable_path_char(*p)) return false;
        if (*p != '/' && *p != '\0') continue;
        size_t segment_length = (size_t)((const char *)p - segment);
        if (segment_length == 0U ||
            segment_length > PASSPORT_APP_STORAGE_SEGMENT_MAX ||
            segment[0] == '.') {
            return false;
        }
        ++depth;
        if (depth > PASSPORT_APP_STORAGE_MAX_DEPTH) return false;
        if (*p == '\0') break;
        segment = (const char *)p + 1;
    }
    return true;
}

uint32_t passport_app_storage_allocated_bytes(size_t file_size)
{
    if (file_size == 0U) return 0U;
    size_t rounded = (file_size + PASSPORT_APP_STORAGE_CLUSTER_BYTES - 1U) /
                     PASSPORT_APP_STORAGE_CLUSTER_BYTES;
    if (rounded > UINT32_MAX / PASSPORT_APP_STORAGE_CLUSTER_BYTES) {
        return UINT32_MAX;
    }
    return (uint32_t)rounded * PASSPORT_APP_STORAGE_CLUSTER_BYTES;
}

bool passport_app_storage_quota_allows(uint32_t current_bytes,
                                       uint32_t old_file_bytes,
                                       size_t new_file_size,
                                       uint16_t current_files,
                                       bool replacing_file)
{
    if (new_file_size > PASSPORT_APP_STORAGE_FILE_MAX ||
        old_file_bytes > current_bytes) {
        return false;
    }
    if (!replacing_file && current_files >= PASSPORT_APP_STORAGE_FILE_COUNT_MAX) {
        return false;
    }
    uint32_t replacement = passport_app_storage_allocated_bytes(new_file_size);
    return replacement <= PASSPORT_APP_STORAGE_QUOTA_BYTES &&
           current_bytes - old_file_bytes <=
               PASSPORT_APP_STORAGE_QUOTA_BYTES - replacement;
}
