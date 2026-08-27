#pragma once

#include "passport_app_storage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_APP_STORAGE_MAX_DEPTH 4U
#define PASSPORT_APP_STORAGE_SEGMENT_MAX 64U
#define PASSPORT_APP_STORAGE_CLUSTER_BYTES 4096U

bool passport_app_storage_path_is_safe(const char *path, bool allow_empty);
uint32_t passport_app_storage_allocated_bytes(size_t file_size);
bool passport_app_storage_quota_allows(uint32_t current_bytes,
                                       uint32_t old_file_bytes,
                                       size_t new_file_size,
                                       uint16_t current_files,
                                       bool replacing_file);
