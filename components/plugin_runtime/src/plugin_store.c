#include "plugin_store.h"

#include "plugin_crypto.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define STORE_PARTITION_LABEL "plugin_store"
#define STAGE_PARTITION_LABEL "plugin_stage"
#define SLOT_MAGIC "PCS4"
#define SLOT_COMMIT_SIZE 64U
#define COPY_CHUNK_SIZE 1024U

typedef struct {
    SemaphoreHandle_t mutex;
    const esp_partition_t *store;
    const esp_partition_t *stage;
    // Store operations are serialized by mutex, so their largest work buffers
    // belong here instead of on the installer task's stack. Keeping these in
    // automatic storage made the commit call chain exceed the worker's stack.
    plugin_record_t records[PLUGIN_STORE_SLOT_COUNT];
    uint8_t copy_buffer[COPY_CHUNK_SIZE];
    size_t stage_expected;
    size_t stage_received;
    bool stage_active;
    bool initialized;
} store_state_t;

static const char *TAG = "plugin_store";
static store_state_t s_store;

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = value & 0xffU;
    data[1] = (value >> 8) & 0xffU;
    data[2] = (value >> 16) & 0xffU;
    data[3] = (value >> 24) & 0xffU;
}

static size_t slot_offset(uint8_t slot)
{
    return (size_t)slot * PLUGIN_STORE_SLOT_SIZE;
}

static esp_err_t read_record(uint8_t slot, plugin_record_t *record)
{
    uint8_t commit[SLOT_COMMIT_SIZE];
    uint8_t package_header_raw[PLUGIN_PACKAGE_HEADER_SIZE];
    uint8_t manifest_raw[PLUGIN_MANIFEST_SIZE];
    plugin_package_header_t package_header;
    plugin_manifest_t manifest;
    size_t base = slot_offset(slot);
    uint32_t package_size;

    if (esp_partition_read(s_store.store, base, commit, sizeof(commit)) != ESP_OK ||
        memcmp(commit, SLOT_MAGIC, 4U) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    package_size = plugin_format_read_u32(commit + 8U);
    if (package_size < PLUGIN_PACKAGE_HEADER_SIZE || package_size > PLUGIN_STORE_PACKAGE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (esp_partition_read(s_store.store, base + PLUGIN_STORE_SLOT_HEADER_SIZE,
                           package_header_raw, sizeof(package_header_raw)) != ESP_OK ||
        plugin_format_parse_package_header(package_header_raw, package_size,
                                           &package_header) != PLUGIN_FORMAT_OK ||
        (uint64_t)package_header.header_size + package_header.content_size != package_size ||
        memcmp(commit + 12U, package_header.digest, PLUGIN_PACKAGE_DIGEST_SIZE) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    if (esp_partition_read(s_store.store,
                           base + PLUGIN_STORE_SLOT_HEADER_SIZE + package_header.header_size,
                           manifest_raw, sizeof(manifest_raw)) != ESP_OK ||
        plugin_format_parse_manifest(manifest_raw, sizeof(manifest_raw),
                                     &manifest) != PLUGIN_FORMAT_OK ||
        plugin_format_validate_manifest_layout(
            &manifest, package_header.content_size) != PLUGIN_FORMAT_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (record) {
        *record = (plugin_record_t) {
            .slot = slot,
            .generation = plugin_format_read_u32(commit + 4U),
            .package_size = package_size,
            .manifest = manifest,
        };
    }
    return ESP_OK;
}

static size_t list_locked(plugin_record_t *records, size_t capacity)
{
    plugin_record_t *found = s_store.records;
    size_t count = 0;

    for (uint8_t slot = 0; slot < PLUGIN_STORE_SLOT_COUNT; ++slot) {
        plugin_record_t candidate;
        if (read_record(slot, &candidate) != ESP_OK) continue;

        size_t duplicate = count;
        for (size_t index = 0; index < count; ++index) {
            if (strcmp(found[index].manifest.id, candidate.manifest.id) == 0) {
                duplicate = index;
                break;
            }
        }
        if (duplicate == count) {
            found[count++] = candidate;
        } else if (candidate.generation > found[duplicate].generation) {
            found[duplicate] = candidate;
        }
    }
    if (records) {
        size_t copy_count = count < capacity ? count : capacity;
        memcpy(records, found, copy_count * sizeof(found[0]));
    }
    return count;
}

esp_err_t plugin_store_init(void)
{
    if (s_store.initialized) return ESP_OK;

    s_store.store = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                             ESP_PARTITION_SUBTYPE_ANY,
                                             STORE_PARTITION_LABEL);
    s_store.stage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                             ESP_PARTITION_SUBTYPE_ANY,
                                             STAGE_PARTITION_LABEL);
    if (!s_store.store || !s_store.stage ||
        s_store.store->size < PLUGIN_STORE_SLOT_COUNT * PLUGIN_STORE_SLOT_SIZE ||
        s_store.stage->size < PLUGIN_STORE_PACKAGE_MAX) {
        ESP_LOGE(TAG, "plugin partitions missing or too small");
        return ESP_ERR_NOT_FOUND;
    }
    s_store.mutex = xSemaphoreCreateMutex();
    if (!s_store.mutex) return ESP_ERR_NO_MEM;
    s_store.initialized = true;
    ESP_LOGI(TAG, "ready: %u slots x %u bytes", PLUGIN_STORE_SLOT_COUNT,
             PLUGIN_STORE_PACKAGE_MAX);
    return ESP_OK;
}

size_t plugin_store_list(plugin_record_t *records, size_t capacity)
{
    size_t count;

    if (!s_store.initialized) return 0;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    count = list_locked(records, capacity);
    xSemaphoreGive(s_store.mutex);
    return count;
}

esp_err_t plugin_store_stage_begin(size_t expected_size)
{
    esp_err_t result;

    if (!s_store.initialized || expected_size < PLUGIN_PACKAGE_HEADER_SIZE ||
        expected_size > PLUGIN_STORE_PACKAGE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    result = esp_partition_erase_range(s_store.stage, 0, s_store.stage->size);
    if (result == ESP_OK) {
        s_store.stage_expected = expected_size;
        s_store.stage_received = 0;
        s_store.stage_active = true;
    }
    xSemaphoreGive(s_store.mutex);
    return result;
}

esp_err_t plugin_store_stage_write(size_t offset, const void *data, size_t size)
{
    esp_err_t result;

    if (!s_store.initialized || (!data && size != 0U)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    if (!s_store.stage_active || offset != s_store.stage_received ||
        size > s_store.stage_expected - s_store.stage_received) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = esp_partition_write(s_store.stage, offset, data, size);
        if (result == ESP_OK) s_store.stage_received += size;
    }
    xSemaphoreGive(s_store.mutex);
    return result;
}

esp_err_t plugin_store_stage_finish(plugin_manifest_t *manifest)
{
    esp_err_t result;

    if (!s_store.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    if (!s_store.stage_active || s_store.stage_received != s_store.stage_expected) {
        result = ESP_ERR_INVALID_SIZE;
    } else {
        result = plugin_crypto_verify_partition(s_store.stage, 0, s_store.stage_expected,
                                                NULL, manifest);
    }
    xSemaphoreGive(s_store.mutex);
    return result;
}

void plugin_store_stage_abort(void)
{
    if (!s_store.initialized) return;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    s_store.stage_active = false;
    s_store.stage_expected = 0;
    s_store.stage_received = 0;
    xSemaphoreGive(s_store.mutex);
}

size_t plugin_store_stage_received(void)
{
    size_t received = 0;
    if (!s_store.initialized) return 0;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    received = s_store.stage_received;
    xSemaphoreGive(s_store.mutex);
    return received;
}

esp_err_t plugin_store_commit_staged(plugin_record_t *record)
{
    plugin_record_t *active = s_store.records;
    plugin_manifest_t staged_manifest;
    plugin_package_header_t staged_header;
    plugin_record_t installed;
    uint8_t commit[SLOT_COMMIT_SIZE];
    bool occupied[PLUGIN_STORE_SLOT_COUNT] = { false };
    int target_slot = -1;
    uint32_t generation = 1;
    size_t active_count;
    esp_err_t result;

    if (!s_store.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    if (!s_store.stage_active || s_store.stage_received != s_store.stage_expected) {
        xSemaphoreGive(s_store.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    result = plugin_crypto_verify_partition(s_store.stage, 0, s_store.stage_expected,
                                            &staged_header, &staged_manifest);
    if (result != ESP_OK) {
        xSemaphoreGive(s_store.mutex);
        return result;
    }
    // list_locked leaves its de-duplicated result in the mutex-protected
    // records workspace when no destination is requested.
    active_count = list_locked(NULL, 0U);
    bool replacing = false;
    for (size_t index = 0; index < active_count; ++index) {
        occupied[active[index].slot] = true;
        if (active[index].generation >= generation) generation = active[index].generation + 1U;
        if (strcmp(active[index].manifest.id, staged_manifest.id) == 0) {
            replacing = true;
            if (staged_manifest.plugin_version < active[index].manifest.plugin_version) {
                xSemaphoreGive(s_store.mutex);
                return ESP_ERR_INVALID_VERSION;
            }
        }
    }
    if (!replacing && active_count >= PLUGIN_STORE_MAX_ACTIVE) {
        xSemaphoreGive(s_store.mutex);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t slot = 0; slot < PLUGIN_STORE_SLOT_COUNT; ++slot) {
        if (!occupied[slot]) {
            target_slot = slot;
            break;
        }
    }
    if (target_slot < 0) {
        xSemaphoreGive(s_store.mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t target_base = slot_offset((uint8_t)target_slot);
    result = esp_partition_erase_range(s_store.store, target_base, PLUGIN_STORE_SLOT_SIZE);
    for (size_t offset = 0; result == ESP_OK && offset < s_store.stage_expected;) {
        size_t length = s_store.stage_expected - offset;
        if (length > sizeof(s_store.copy_buffer)) length = sizeof(s_store.copy_buffer);
        result = esp_partition_read(s_store.stage, offset, s_store.copy_buffer, length);
        if (result == ESP_OK) {
            result = esp_partition_write(s_store.store,
                                         target_base + PLUGIN_STORE_SLOT_HEADER_SIZE + offset,
                                         s_store.copy_buffer, length);
        }
        offset += length;
    }
    if (result == ESP_OK) {
        result = plugin_crypto_verify_partition(
            s_store.store, target_base + PLUGIN_STORE_SLOT_HEADER_SIZE,
            s_store.stage_expected, NULL, NULL);
    }
    if (result == ESP_OK) {
        memset(commit, 0xff, sizeof(commit));
        write_u32(commit + 4U, generation);
        write_u32(commit + 8U, (uint32_t)s_store.stage_expected);
        memcpy(commit + 12U, staged_header.digest, sizeof(staged_header.digest));
        result = esp_partition_write(s_store.store, target_base + 4U,
                                     commit + 4U, sizeof(commit) - 4U);
        if (result == ESP_OK) {
            result = esp_partition_write(s_store.store, target_base, SLOT_MAGIC, 4U);
        }
    }
    if (result == ESP_OK) {
        installed = (plugin_record_t) {
            .slot = (uint8_t)target_slot,
            .generation = generation,
            .package_size = (uint32_t)s_store.stage_expected,
            .manifest = staged_manifest,
        };
        for (size_t index = 0; index < active_count; ++index) {
            if (strcmp(active[index].manifest.id, staged_manifest.id) == 0 &&
                active[index].slot != (uint8_t)target_slot) {
                esp_partition_erase_range(s_store.store, slot_offset(active[index].slot),
                                          PLUGIN_STORE_SLOT_SIZE);
            }
        }
        s_store.stage_active = false;
        if (record) *record = installed;
        ESP_LOGI(TAG, "installed %s v%lu in slot %d", staged_manifest.id,
                 (unsigned long)staged_manifest.plugin_version, target_slot);
    }
    xSemaphoreGive(s_store.mutex);
    return result;
}

esp_err_t plugin_store_remove(const char *plugin_id)
{
    esp_err_t result = ESP_ERR_NOT_FOUND;

    if (!s_store.initialized) return ESP_ERR_INVALID_STATE;
    if (!plugin_id || plugin_id[0] == '\0') return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    for (uint8_t slot = 0; slot < PLUGIN_STORE_SLOT_COUNT; ++slot) {
        plugin_record_t candidate;
        if (read_record(slot, &candidate) == ESP_OK &&
            strcmp(candidate.manifest.id, plugin_id) == 0) {
            ESP_LOGI(TAG, "removing %s from slot %u", plugin_id, (unsigned)slot);
            esp_err_t erase_result = esp_partition_erase_range(
                s_store.store, slot_offset(slot), PLUGIN_STORE_SLOT_HEADER_SIZE);
            if (erase_result == ESP_OK) {
                uint8_t magic[sizeof(SLOT_MAGIC) - 1U];
                erase_result = esp_partition_read(
                    s_store.store, slot_offset(slot), magic, sizeof(magic));
                if (erase_result == ESP_OK &&
                    memcmp(magic, SLOT_MAGIC, sizeof(magic)) == 0) {
                    erase_result = ESP_FAIL;
                }
            }
            if (erase_result != ESP_OK) {
                ESP_LOGE(TAG, "remove %s slot %u failed: %s", plugin_id,
                         (unsigned)slot, esp_err_to_name(erase_result));
                if (result == ESP_ERR_NOT_FOUND || result == ESP_OK) result = erase_result;
            } else if (result == ESP_ERR_NOT_FOUND) {
                result = ESP_OK;
            }
        }
    }
    if (result == ESP_OK) {
        for (uint8_t slot = 0; slot < PLUGIN_STORE_SLOT_COUNT; ++slot) {
            plugin_record_t candidate;
            if (read_record(slot, &candidate) == ESP_OK &&
                strcmp(candidate.manifest.id, plugin_id) == 0) {
                result = ESP_FAIL;
                ESP_LOGE(TAG, "remove %s verification found slot %u still active",
                         plugin_id, (unsigned)slot);
                break;
            }
        }
    }
    xSemaphoreGive(s_store.mutex);
    if (result == ESP_OK) ESP_LOGI(TAG, "removed %s and verified inactive", plugin_id);
    return result;
}

esp_err_t plugin_store_open(const plugin_record_t *record, plugin_image_t *image)
{
    const void *mapped = NULL;
    esp_partition_mmap_handle_t mapping = 0;
    plugin_package_header_t package_header;
    plugin_manifest_t manifest;
    size_t offset;
    esp_err_t result;

    if (!s_store.initialized || !record || !image || record->slot >= PLUGIN_STORE_SLOT_COUNT)
        return ESP_ERR_INVALID_ARG;
    offset = slot_offset(record->slot) + PLUGIN_STORE_SLOT_HEADER_SIZE;
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
    result = plugin_crypto_verify_partition(s_store.store, offset, record->package_size,
                                            &package_header, &manifest);
    if (result == ESP_OK) {
        result = esp_partition_mmap(s_store.store, offset, record->package_size,
                                    ESP_PARTITION_MMAP_DATA, &mapped, &mapping);
    }
    xSemaphoreGive(s_store.mutex);
    if (result != ESP_OK) return result;

    *image = (plugin_image_t) {
        .record = *record,
        .package = mapped,
        .content = (const uint8_t *)mapped + package_header.header_size,
        .mapping = mapping,
    };
    image->record.manifest = manifest;
    return ESP_OK;
}

void plugin_store_close(plugin_image_t *image)
{
    if (!image || !image->package) return;
    esp_partition_munmap(image->mapping);
    memset(image, 0, sizeof(*image));
}
