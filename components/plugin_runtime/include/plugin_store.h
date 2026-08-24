#pragma once

#include "plugin_format.h"

#include "esp_err.h"
#include "esp_partition.h"

#include <stddef.h>
#include <stdint.h>

#define PLUGIN_STORE_SLOT_COUNT 8U
#define PLUGIN_STORE_MAX_ACTIVE 7U
#define PLUGIN_STORE_SLOT_SIZE 0x40000U
#define PLUGIN_STORE_SLOT_HEADER_SIZE 0x1000U
#define PLUGIN_STORE_PACKAGE_MAX (PLUGIN_STORE_SLOT_SIZE - PLUGIN_STORE_SLOT_HEADER_SIZE)

typedef struct {
    uint8_t slot;
    uint32_t generation;
    uint32_t package_size;
    plugin_manifest_t manifest;
} plugin_record_t;

typedef struct {
    plugin_record_t record;
    const uint8_t *package;
    const uint8_t *content;
    esp_partition_mmap_handle_t mapping;
} plugin_image_t;

esp_err_t plugin_store_init(void);
size_t plugin_store_list(plugin_record_t *records, size_t capacity);
esp_err_t plugin_store_stage_begin(size_t expected_size);
esp_err_t plugin_store_stage_write(size_t offset, const void *data, size_t size);
esp_err_t plugin_store_stage_finish(plugin_manifest_t *manifest);
void plugin_store_stage_abort(void);
size_t plugin_store_stage_received(void);
esp_err_t plugin_store_commit_staged(plugin_record_t *record);
esp_err_t plugin_store_remove(const char *plugin_id);
esp_err_t plugin_store_open(const plugin_record_t *record, plugin_image_t *image);
void plugin_store_close(plugin_image_t *image);
