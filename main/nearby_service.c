#include "nearby_service.h"

#include "bsp_audio.h"
#include "nearby_protocol.h"
#include "plugin_ble.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define NEARBY_WORK_DEPTH 16U
#define NEARBY_EVENT_DEPTH 16U
#define NEARBY_VOICE_QUEUE_DEPTH 6U
#define NEARBY_PARTITION_LABEL "nearby_data"
#define NEARBY_PARTITION_SUBTYPE 0x42U
#define NEARBY_BLOB_NAME_MAX 63U
#define NEARBY_BLOB_MIME_MAX 47U
#define NEARBY_BLOB_METADATA_FIXED 34U
#define NEARBY_HANDLE_BUFFER 0x10000000U
#define NEARBY_HANDLE_OBJECT 0x20000000U
#define NEARBY_HANDLE_TYPE_MASK 0xf0000000U
#define NEARBY_SEND_RETRIES 8U

typedef enum {
    WORK_FRAME,
    WORK_STATE,
    WORK_SEND_MESSAGE,
    WORK_SEND_BLOB_OFFER,
    WORK_ACCEPT_BLOB,
    WORK_REJECT_BLOB,
} nearby_work_type_t;

typedef struct {
    nearby_work_type_t type;
    uint32_t generation;
    int32_t handle;
    uint32_t id;
    plugin_ble_sync_state_t state;
    uint16_t size;
    char name[NEARBY_BLOB_NAME_MAX + 1U];
    char mime[NEARBY_BLOB_MIME_MAX + 1U];
    uint8_t data[NEARBY_FRAME_HEADER_SIZE + NEARBY_FRAME_PAYLOAD_MAX];
} nearby_work_t;

typedef struct {
    bool used;
    bool plugin_owned;
    uint16_t capacity;
    uint16_t length;
    uint16_t busy;
    uint32_t serial;
    uint8_t data[NEARBY_BUFFER_CAPACITY_MAX];
} nearby_buffer_t;

typedef struct {
    bool valid;
    bool plugin_owned;
    uint16_t busy;
    uint32_t serial;
    uint32_t size;
    uint8_t digest[32];
    char name[NEARBY_BLOB_NAME_MAX + 1U];
    char mime[NEARBY_BLOB_MIME_MAX + 1U];
} nearby_object_t;

typedef struct {
    bool pending;
    bool receiving;
    bool sha_active;
    uint32_t id;
    uint32_t total;
    uint32_t received;
    int32_t metadata_handle;
    uint8_t digest[32];
    char name[NEARBY_BLOB_NAME_MAX + 1U];
    char mime[NEARBY_BLOB_MIME_MAX + 1U];
    mbedtls_sha256_context sha;
} incoming_blob_t;

typedef struct {
    bool active;
    bool waiting_ack;
    uint32_t id;
    int32_t handle;
    uint32_t size;
    uint8_t digest[32];
    char name[NEARBY_BLOB_NAME_MAX + 1U];
    char mime[NEARBY_BLOB_MIME_MAX + 1U];
} outgoing_blob_t;

typedef struct {
    uint32_t generation;
    uint8_t data[NEARBY_VOICE_BLOCK_SIZE];
} voice_packet_t;

typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t tx_mutex;
    QueueHandle_t work_queue;
    QueueHandle_t event_queue;
    QueueHandle_t voice_queue;
    const esp_partition_t *partition;
    nearby_buffer_t buffers[NEARBY_BUFFER_COUNT];
    nearby_object_t object;
    incoming_blob_t incoming;
    outgoing_blob_t outgoing;
    uint32_t foreground_owner;
    uint32_t generation;
    uint32_t handle_serial;
    uint32_t next_message_id;
    uint32_t next_transfer_id;
    uint32_t rx_message_id;
    uint32_t rx_message_total;
    uint32_t rx_message_received;
    int32_t rx_message_handle;
    uint32_t voice_sequence;
    bool initialized;
    bool audio_available;
    bool lease;
    bool voice_active;
    bool voice_transmitting;
} nearby_state_t;

static const char *TAG = "nearby";
static nearby_state_t s_nearby;

static void reset_incoming_locked(void);

static uint32_t next_nonzero(uint32_t value)
{
    ++value;
    return value == 0U ? 1U : value;
}

static int32_t buffer_handle(size_t index, uint32_t serial)
{
    return (int32_t)(NEARBY_HANDLE_BUFFER |
                     ((serial & 0x000fffffU) << 4) |
                     ((uint32_t)index + 1U));
}

static int32_t object_handle(uint32_t serial)
{
    return (int32_t)(NEARBY_HANDLE_OBJECT | (serial & 0x0fffffffU));
}

static bool owner_matches_locked(uint32_t owner)
{
    return owner != 0U && s_nearby.foreground_owner == owner;
}

static nearby_buffer_t *find_buffer_locked(int32_t handle, bool plugin_access,
                                           size_t *index_out)
{
    uint32_t bits = (uint32_t)handle;
    if ((bits & NEARBY_HANDLE_TYPE_MASK) != NEARBY_HANDLE_BUFFER) return NULL;
    uint32_t raw_index = bits & 0x0fU;
    if (raw_index == 0U || raw_index > NEARBY_BUFFER_COUNT) return NULL;
    size_t index = raw_index - 1U;
    nearby_buffer_t *buffer = &s_nearby.buffers[index];
    uint32_t serial = (bits >> 4) & 0x000fffffU;
    if (!buffer->used || (buffer->serial & 0x000fffffU) != serial ||
        (plugin_access && !buffer->plugin_owned)) {
        return NULL;
    }
    if (index_out) *index_out = index;
    return buffer;
}

static bool object_matches_locked(int32_t handle, bool plugin_access)
{
    uint32_t bits = (uint32_t)handle;
    return (bits & NEARBY_HANDLE_TYPE_MASK) == NEARBY_HANDLE_OBJECT &&
           s_nearby.object.valid &&
           (bits & 0x0fffffffU) == (s_nearby.object.serial & 0x0fffffffU) &&
           (!plugin_access || s_nearby.object.plugin_owned);
}

static bool allocate_buffer_locked(uint16_t capacity, int32_t *handle)
{
    if (capacity == 0U || capacity > NEARBY_BUFFER_CAPACITY_MAX || !handle) {
        return false;
    }
    for (size_t index = 0; index < NEARBY_BUFFER_COUNT; ++index) {
        nearby_buffer_t *buffer = &s_nearby.buffers[index];
        if (buffer->used) continue;
        memset(buffer, 0, sizeof(*buffer));
        s_nearby.handle_serial = next_nonzero(s_nearby.handle_serial);
        buffer->serial = s_nearby.handle_serial;
        buffer->capacity = capacity;
        buffer->used = true;
        buffer->plugin_owned = true;
        *handle = buffer_handle(index, buffer->serial);
        return true;
    }
    return false;
}

static void release_handle_locked(int32_t handle)
{
    nearby_buffer_t *buffer = find_buffer_locked(handle, false, NULL);
    if (buffer) {
        if (buffer->busy > 0U) buffer->plugin_owned = false;
        else memset(buffer, 0, sizeof(*buffer));
        return;
    }
    if (object_matches_locked(handle, false)) {
        if (s_nearby.object.busy > 0U) s_nearby.object.plugin_owned = false;
        else memset(&s_nearby.object, 0, sizeof(s_nearby.object));
    }
}

static bool retain_handle_locked(int32_t handle, uint32_t *size)
{
    nearby_buffer_t *buffer = find_buffer_locked(handle, true, NULL);
    if (buffer) {
        ++buffer->busy;
        if (size) *size = buffer->length;
        return true;
    }
    if (object_matches_locked(handle, true)) {
        ++s_nearby.object.busy;
        if (size) *size = s_nearby.object.size;
        return true;
    }
    return false;
}

static void unretain_handle_locked(int32_t handle)
{
    nearby_buffer_t *buffer = find_buffer_locked(handle, false, NULL);
    if (buffer) {
        if (buffer->busy > 0U) --buffer->busy;
        if (buffer->busy == 0U && !buffer->plugin_owned) {
            memset(buffer, 0, sizeof(*buffer));
        }
        return;
    }
    if (object_matches_locked(handle, false)) {
        if (s_nearby.object.busy > 0U) --s_nearby.object.busy;
        if (s_nearby.object.busy == 0U && !s_nearby.object.plugin_owned) {
            memset(&s_nearby.object, 0, sizeof(s_nearby.object));
        }
    }
}

static void discard_event_locked(const nearby_event_t *event)
{
    if (event->type == NEARBY_EVENT_BLOB_OFFER &&
        s_nearby.incoming.pending &&
        s_nearby.incoming.id == (uint32_t)event->id) {
        reset_incoming_locked();
        return;
    }
    if (event->handle != 0) release_handle_locked(event->handle);
}

static void emit_event(uint32_t generation, nearby_event_type_t type,
                       int32_t id, int32_t handle, int32_t value)
{
    nearby_event_t event = {
        .type = type,
        .id = id,
        .handle = handle,
        .value = value,
    };
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool current = s_nearby.foreground_owner != 0U &&
                   s_nearby.generation == generation;
    if (!current) discard_event_locked(&event);
    xSemaphoreGive(s_nearby.mutex);
    if (!current) return;
    if (xQueueSend(s_nearby.event_queue, &event, 0) != pdTRUE) {
        nearby_event_t discarded = {0};
        if (xQueueReceive(s_nearby.event_queue, &discarded, 0) == pdTRUE) {
            xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
            discard_event_locked(&discarded);
            xSemaphoreGive(s_nearby.mutex);
        }
        if (xQueueSend(s_nearby.event_queue, &event, 0) != pdTRUE) {
            xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
            discard_event_locked(&event);
            xSemaphoreGive(s_nearby.mutex);
        }
        ESP_LOGW(TAG, "event queue full; oldest event discarded");
    }
}

static void emit_error(uint32_t generation, uint32_t id, nearby_error_t error)
{
    emit_event(generation, NEARBY_EVENT_ERROR, (int32_t)id, 0, error);
}

static bool current_generation(uint32_t generation)
{
    bool current;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    current = s_nearby.foreground_owner != 0U &&
              s_nearby.generation == generation && s_nearby.lease;
    xSemaphoreGive(s_nearby.mutex);
    return current;
}

static bool send_frame(uint32_t generation, uint8_t type, uint8_t flags,
                       uint32_t id, uint32_t offset, uint32_t total,
                       const uint8_t *payload, size_t payload_size)
{
    uint8_t packet[NEARBY_FRAME_HEADER_SIZE + NEARBY_FRAME_PAYLOAD_MAX];
    size_t size = nearby_frame_encode(packet, sizeof(packet), type, flags,
                                      id, offset, total, payload, payload_size);
    if (size == 0U) return false;
    for (size_t attempt = 0; attempt < NEARBY_SEND_RETRIES; ++attempt) {
        if (!current_generation(generation)) return false;
        xSemaphoreTake(s_nearby.tx_mutex, portMAX_DELAY);
        bool sent = plugin_ble_runtime_send(packet, size);
        xSemaphoreGive(s_nearby.tx_mutex);
        if (sent) return true;
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    return false;
}

static bool handle_read(int32_t handle, uint32_t offset,
                        uint8_t *output, size_t size)
{
    bool ok = false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = find_buffer_locked(handle, false, NULL);
    if (buffer && offset <= buffer->length && size <= buffer->length - offset) {
        memcpy(output, buffer->data + offset, size);
        ok = true;
    } else if (object_matches_locked(handle, false) &&
               offset <= s_nearby.object.size &&
               size <= s_nearby.object.size - offset) {
        ok = esp_partition_read(s_nearby.partition, offset, output, size) == ESP_OK;
    }
    xSemaphoreGive(s_nearby.mutex);
    return ok;
}

static bool handle_digest(int32_t handle, uint8_t digest[32])
{
    bool ok = false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = find_buffer_locked(handle, false, NULL);
    if (buffer) {
        ok = mbedtls_sha256(buffer->data, buffer->length, digest, 0) == 0;
    } else if (object_matches_locked(handle, false)) {
        memcpy(digest, s_nearby.object.digest, 32U);
        ok = true;
    }
    xSemaphoreGive(s_nearby.mutex);
    return ok;
}

static bool send_handle_data(uint32_t generation, uint8_t frame_type,
                             uint32_t id, int32_t handle, uint32_t total)
{
    uint8_t chunk[NEARBY_FRAME_PAYLOAD_MAX];
    if (total == 0U) {
        return send_frame(generation, frame_type,
                          NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST,
                          id, 0U, 0U, NULL, 0U);
    }
    for (uint32_t offset = 0U; offset < total;) {
        size_t size = total - offset;
        if (size > sizeof(chunk)) size = sizeof(chunk);
        if (!handle_read(handle, offset, chunk, size)) return false;
        uint8_t flags = 0U;
        if (offset == 0U) flags |= NEARBY_FRAME_FLAG_FIRST;
        if (offset + size == total) flags |= NEARBY_FRAME_FLAG_LAST;
        if (!send_frame(generation, frame_type, flags, id, offset, total,
                        chunk, size)) {
            return false;
        }
        offset += size;
    }
    return true;
}

static void reset_message_locked(void)
{
    if (s_nearby.rx_message_handle != 0) {
        release_handle_locked(s_nearby.rx_message_handle);
    }
    s_nearby.rx_message_handle = 0;
    s_nearby.rx_message_id = 0U;
    s_nearby.rx_message_total = 0U;
    s_nearby.rx_message_received = 0U;
}

static void reset_incoming_locked(void)
{
    if (s_nearby.incoming.sha_active) {
        mbedtls_sha256_free(&s_nearby.incoming.sha);
    }
    if (s_nearby.incoming.metadata_handle != 0) {
        release_handle_locked(s_nearby.incoming.metadata_handle);
    }
    memset(&s_nearby.incoming, 0, sizeof(s_nearby.incoming));
}

static void reset_outgoing_locked(void)
{
    if (s_nearby.outgoing.active) {
        unretain_handle_locked(s_nearby.outgoing.handle);
    }
    memset(&s_nearby.outgoing, 0, sizeof(s_nearby.outgoing));
}

static bool metadata_text_valid(const uint8_t *data, size_t size)
{
    if (!data || size == 0U) return false;
    for (size_t index = 0; index < size; ++index) {
        if (data[index] == 0U || data[index] < 0x20U || data[index] == 0x7fU) {
            return false;
        }
    }
    return true;
}

static void process_message(uint32_t generation, const nearby_frame_t *frame)
{
    bool complete = false;
    int32_t completed_handle = 0;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if ((frame->flags & NEARBY_FRAME_FLAG_FIRST) != 0U) {
        reset_message_locked();
        if (frame->offset != 0U || frame->total > NEARBY_BUFFER_CAPACITY_MAX ||
            !allocate_buffer_locked((uint16_t)(frame->total == 0U ? 1U : frame->total),
                                    &s_nearby.rx_message_handle)) {
            xSemaphoreGive(s_nearby.mutex);
            emit_error(generation, frame->id,
                       frame->total > NEARBY_BUFFER_CAPACITY_MAX ?
                       NEARBY_ERROR_TOO_LARGE : NEARBY_ERROR_NO_BUFFER);
            return;
        }
        s_nearby.rx_message_id = frame->id;
        s_nearby.rx_message_total = frame->total;
    }
    nearby_buffer_t *buffer = find_buffer_locked(
        s_nearby.rx_message_handle, false, NULL);
    if (!buffer || s_nearby.rx_message_id != frame->id ||
        s_nearby.rx_message_total != frame->total ||
        frame->offset != s_nearby.rx_message_received ||
        frame->offset > frame->total ||
        frame->payload_size > frame->total - frame->offset) {
        reset_message_locked();
        xSemaphoreGive(s_nearby.mutex);
        emit_error(generation, frame->id, NEARBY_ERROR_OUT_OF_ORDER);
        return;
    }
    if (frame->payload_size > 0U) {
        memcpy(buffer->data + frame->offset, frame->payload, frame->payload_size);
    }
    s_nearby.rx_message_received += frame->payload_size;
    buffer->length = (uint16_t)s_nearby.rx_message_received;
    complete = (frame->flags & NEARBY_FRAME_FLAG_LAST) != 0U &&
               s_nearby.rx_message_received == frame->total;
    if (complete) {
        completed_handle = s_nearby.rx_message_handle;
        s_nearby.rx_message_handle = 0;
        s_nearby.rx_message_id = 0U;
        s_nearby.rx_message_total = 0U;
        s_nearby.rx_message_received = 0U;
    }
    xSemaphoreGive(s_nearby.mutex);
    if (complete) {
        emit_event(generation, NEARBY_EVENT_MESSAGE, (int32_t)frame->id,
                   completed_handle, (int32_t)frame->total);
    }
}

static void process_blob_offer(uint32_t generation, const nearby_frame_t *frame)
{
    if (frame->id == 0U || frame->offset != 0U ||
        frame->flags != (NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST) ||
        frame->payload_size < NEARBY_BLOB_METADATA_FIXED ||
        !s_nearby.partition || frame->total == 0U ||
        frame->total > s_nearby.partition->size) {
        emit_error(generation, frame->id,
                   frame->total > (s_nearby.partition ? s_nearby.partition->size : 0U) ?
                   NEARBY_ERROR_TOO_LARGE : NEARBY_ERROR_PROTOCOL);
        return;
    }
    uint8_t name_length = frame->payload[32U];
    uint8_t mime_length = frame->payload[33U];
    if (name_length == 0U || name_length > NEARBY_BLOB_NAME_MAX ||
        mime_length == 0U || mime_length > NEARBY_BLOB_MIME_MAX ||
        frame->payload_size != NEARBY_BLOB_METADATA_FIXED +
                               name_length + mime_length ||
        !metadata_text_valid(frame->payload + NEARBY_BLOB_METADATA_FIXED,
                             name_length) ||
        !metadata_text_valid(frame->payload + NEARBY_BLOB_METADATA_FIXED + name_length,
                             mime_length)) {
        emit_error(generation, frame->id, NEARBY_ERROR_PROTOCOL);
        return;
    }

    int32_t metadata_handle = 0;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool busy = s_nearby.incoming.pending || s_nearby.incoming.receiving ||
                s_nearby.outgoing.active;
    if (busy ||
        !allocate_buffer_locked((uint16_t)(name_length + 1U + mime_length),
                                &metadata_handle)) {
        xSemaphoreGive(s_nearby.mutex);
        emit_error(generation, frame->id,
                   busy ? NEARBY_ERROR_BUSY : NEARBY_ERROR_NO_BUFFER);
        return;
    }
    nearby_buffer_t *metadata = find_buffer_locked(metadata_handle, false, NULL);
    memcpy(metadata->data, frame->payload + NEARBY_BLOB_METADATA_FIXED, name_length);
    metadata->data[name_length] = '\n';
    memcpy(metadata->data + name_length + 1U,
           frame->payload + NEARBY_BLOB_METADATA_FIXED + name_length,
           mime_length);
    metadata->length = (uint16_t)(name_length + 1U + mime_length);
    s_nearby.incoming.pending = true;
    s_nearby.incoming.id = frame->id;
    s_nearby.incoming.total = frame->total;
    s_nearby.incoming.metadata_handle = metadata_handle;
    memcpy(s_nearby.incoming.digest, frame->payload, 32U);
    memcpy(s_nearby.incoming.name,
           frame->payload + NEARBY_BLOB_METADATA_FIXED, name_length);
    s_nearby.incoming.name[name_length] = '\0';
    memcpy(s_nearby.incoming.mime,
           frame->payload + NEARBY_BLOB_METADATA_FIXED + name_length,
           mime_length);
    s_nearby.incoming.mime[mime_length] = '\0';
    xSemaphoreGive(s_nearby.mutex);
    emit_event(generation, NEARBY_EVENT_BLOB_OFFER, (int32_t)frame->id,
               metadata_handle, (int32_t)frame->total);
}

static void process_blob_data(uint32_t generation, const nearby_frame_t *frame)
{
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = s_nearby.incoming.receiving &&
                 s_nearby.incoming.id == frame->id &&
                 s_nearby.incoming.total == frame->total &&
                 s_nearby.incoming.received == frame->offset &&
                 frame->offset <= frame->total &&
                 frame->payload_size <= frame->total - frame->offset;
    if (!valid) {
        xSemaphoreGive(s_nearby.mutex);
        emit_error(generation, frame->id, NEARBY_ERROR_OUT_OF_ORDER);
        return;
    }
    esp_err_t result = esp_partition_write(s_nearby.partition, frame->offset,
                                           frame->payload, frame->payload_size);
    if (result == ESP_OK && frame->payload_size > 0U) {
        result = mbedtls_sha256_update(&s_nearby.incoming.sha,
                                      frame->payload,
                                      frame->payload_size) == 0 ?
                 ESP_OK : ESP_FAIL;
    }
    if (result == ESP_OK) s_nearby.incoming.received += frame->payload_size;
    uint32_t received = s_nearby.incoming.received;
    xSemaphoreGive(s_nearby.mutex);
    if (result != ESP_OK) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        if (s_nearby.incoming.id == frame->id) reset_incoming_locked();
        xSemaphoreGive(s_nearby.mutex);
        send_frame(generation, NEARBY_FRAME_BLOB_CANCEL, 0U,
                   frame->id, received, frame->total, NULL, 0U);
        emit_error(generation, frame->id, NEARBY_ERROR_FLASH);
        return;
    }
    if (received == frame->total || (received & 0x3fffU) < frame->payload_size) {
        emit_event(generation, NEARBY_EVENT_BLOB_PROGRESS, (int32_t)frame->id,
                   0, (int32_t)received);
    }
}

static void process_blob_complete(uint32_t generation, const nearby_frame_t *frame)
{
    uint8_t digest[32];
    int32_t handle = 0;
    bool valid = false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (s_nearby.incoming.receiving && s_nearby.incoming.id == frame->id &&
        s_nearby.incoming.sha_active &&
        frame->flags == NEARBY_FRAME_FLAG_LAST &&
        frame->total == s_nearby.incoming.total &&
        frame->offset == frame->total && frame->payload_size == sizeof(digest) &&
        s_nearby.incoming.received == s_nearby.incoming.total &&
        mbedtls_sha256_finish(&s_nearby.incoming.sha, digest) == 0) {
        s_nearby.incoming.sha_active = false;
        mbedtls_sha256_free(&s_nearby.incoming.sha);
        valid = memcmp(digest, s_nearby.incoming.digest, sizeof(digest)) == 0 &&
                memcmp(digest, frame->payload, sizeof(digest)) == 0;
        if (valid) {
            memset(&s_nearby.object, 0, sizeof(s_nearby.object));
            s_nearby.handle_serial = next_nonzero(s_nearby.handle_serial);
            s_nearby.object.valid = true;
            s_nearby.object.plugin_owned = true;
            s_nearby.object.serial = s_nearby.handle_serial;
            s_nearby.object.size = s_nearby.incoming.total;
            memcpy(s_nearby.object.digest, digest, sizeof(digest));
            snprintf(s_nearby.object.name, sizeof(s_nearby.object.name), "%s",
                     s_nearby.incoming.name);
            snprintf(s_nearby.object.mime, sizeof(s_nearby.object.mime), "%s",
                     s_nearby.incoming.mime);
            handle = object_handle(s_nearby.object.serial);
        }
    }
    uint32_t total = s_nearby.incoming.total;
    reset_incoming_locked();
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) {
        emit_error(generation, frame->id, NEARBY_ERROR_DIGEST);
        return;
    }
    send_frame(generation, NEARBY_FRAME_ACK, NEARBY_FRAME_FLAG_ACCEPT,
               frame->id, total, total, digest, sizeof(digest));
    emit_event(generation, NEARBY_EVENT_BLOB_READY, (int32_t)frame->id,
               handle, (int32_t)total);
}

static void finish_outgoing(uint32_t generation, nearby_event_type_t event_type,
                            nearby_error_t error)
{
    uint32_t id;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    id = s_nearby.outgoing.id;
    reset_outgoing_locked();
    xSemaphoreGive(s_nearby.mutex);
    if (error != 0) emit_error(generation, id, error);
    else emit_event(generation, event_type, (int32_t)id, 0, 0);
}

static void process_blob_decision(uint32_t generation, const nearby_frame_t *frame)
{
    outgoing_blob_t outgoing;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = s_nearby.outgoing.active &&
                 !s_nearby.outgoing.waiting_ack &&
                 s_nearby.outgoing.id == frame->id &&
                 frame->offset == 0U && frame->total == 0U &&
                 frame->payload_size == 0U &&
                 (frame->flags == 0U ||
                  frame->flags == NEARBY_FRAME_FLAG_ACCEPT);
    outgoing = s_nearby.outgoing;
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) {
        emit_error(generation, frame->id, NEARBY_ERROR_PROTOCOL);
        return;
    }
    if ((frame->flags & NEARBY_FRAME_FLAG_ACCEPT) == 0U) {
        finish_outgoing(generation, NEARBY_EVENT_BLOB_REJECTED, 0);
        return;
    }
    bool sent = send_handle_data(generation, NEARBY_FRAME_BLOB_DATA,
                                 outgoing.id, outgoing.handle, outgoing.size) &&
                send_frame(generation, NEARBY_FRAME_BLOB_COMPLETE,
                           NEARBY_FRAME_FLAG_LAST, outgoing.id,
                           outgoing.size, outgoing.size,
                           outgoing.digest, sizeof(outgoing.digest));
    if (sent) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        sent = s_nearby.outgoing.active &&
               s_nearby.outgoing.id == outgoing.id;
        if (sent) s_nearby.outgoing.waiting_ack = true;
        xSemaphoreGive(s_nearby.mutex);
    }
    if (!sent) {
        finish_outgoing(generation, NEARBY_EVENT_BLOB_SENT,
                        NEARBY_ERROR_TRANSPORT);
    }
}

static void process_blob_ack(uint32_t generation, const nearby_frame_t *frame)
{
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool transfer = s_nearby.outgoing.active &&
                    s_nearby.outgoing.waiting_ack &&
                    s_nearby.outgoing.id == frame->id;
    bool valid = transfer && frame->flags == NEARBY_FRAME_FLAG_ACCEPT &&
                 frame->offset == s_nearby.outgoing.size &&
                 frame->total == s_nearby.outgoing.size &&
                 frame->payload_size == sizeof(s_nearby.outgoing.digest) &&
                 memcmp(frame->payload, s_nearby.outgoing.digest,
                        sizeof(s_nearby.outgoing.digest)) == 0;
    xSemaphoreGive(s_nearby.mutex);
    if (!transfer) {
        emit_error(generation, frame->id, NEARBY_ERROR_PROTOCOL);
    } else if (!valid) {
        finish_outgoing(generation, NEARBY_EVENT_BLOB_SENT,
                        NEARBY_ERROR_DIGEST);
    } else {
        finish_outgoing(generation, NEARBY_EVENT_BLOB_SENT, 0);
    }
}

static void process_voice(uint32_t generation, const nearby_frame_t *frame)
{
    if (frame->payload_size != NEARBY_VOICE_BLOCK_SIZE) {
        emit_error(generation, frame->id, NEARBY_ERROR_PROTOCOL);
        return;
    }
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool receive = s_nearby.voice_active && !s_nearby.voice_transmitting &&
                   s_nearby.generation == generation;
    xSemaphoreGive(s_nearby.mutex);
    if (!receive) return;
    voice_packet_t packet = { .generation = generation };
    memcpy(packet.data, frame->payload, sizeof(packet.data));
    if (xQueueSend(s_nearby.voice_queue, &packet, 0) != pdTRUE) {
        voice_packet_t discarded;
        xQueueReceive(s_nearby.voice_queue, &discarded, 0);
        xQueueSend(s_nearby.voice_queue, &packet, 0);
    }
}

static void process_frame(uint32_t generation, const uint8_t *data, size_t size)
{
    nearby_frame_t frame;
    if (!nearby_frame_decode(data, size, &frame)) {
        emit_error(generation, 0U, NEARBY_ERROR_PROTOCOL);
        return;
    }
    if (frame.id == 0U || frame.id > INT32_MAX) {
        emit_error(generation, 0U, NEARBY_ERROR_PROTOCOL);
        return;
    }
    switch (frame.type) {
    case NEARBY_FRAME_MESSAGE:
        process_message(generation, &frame);
        break;
    case NEARBY_FRAME_BLOB_OFFER:
        process_blob_offer(generation, &frame);
        break;
    case NEARBY_FRAME_BLOB_DECISION:
        process_blob_decision(generation, &frame);
        break;
    case NEARBY_FRAME_BLOB_DATA:
        process_blob_data(generation, &frame);
        break;
    case NEARBY_FRAME_BLOB_COMPLETE:
        process_blob_complete(generation, &frame);
        break;
    case NEARBY_FRAME_BLOB_CANCEL:
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        if (s_nearby.incoming.id == frame.id) reset_incoming_locked();
        if (s_nearby.outgoing.id == frame.id) reset_outgoing_locked();
        xSemaphoreGive(s_nearby.mutex);
        emit_event(generation, NEARBY_EVENT_BLOB_REJECTED,
                   (int32_t)frame.id, 0, 0);
        break;
    case NEARBY_FRAME_VOICE:
        process_voice(generation, &frame);
        break;
    case NEARBY_FRAME_ACK:
        process_blob_ack(generation, &frame);
        break;
    case NEARBY_FRAME_ERROR:
        break;
    default:
        emit_error(generation, frame.id, NEARBY_ERROR_PROTOCOL);
        break;
    }
}

static void handle_state(uint32_t generation, plugin_ble_sync_state_t state)
{
    if (state == PLUGIN_BLE_DISCONNECTED) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        reset_message_locked();
        reset_incoming_locked();
        reset_outgoing_locked();
        s_nearby.voice_active = false;
        s_nearby.voice_transmitting = false;
        xSemaphoreGive(s_nearby.mutex);
    }
    emit_event(generation, NEARBY_EVENT_STATE, 0, 0, state);
}

static void process_send_message(const nearby_work_t *work)
{
    uint32_t size;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = find_buffer_locked(work->handle, false, NULL);
    bool exists = buffer != NULL;
    size = buffer ? buffer->length : 0U;
    xSemaphoreGive(s_nearby.mutex);
    bool sent = exists && send_handle_data(work->generation, NEARBY_FRAME_MESSAGE,
                                           work->id, work->handle, size);
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    unretain_handle_locked(work->handle);
    xSemaphoreGive(s_nearby.mutex);
    if (sent) {
        emit_event(work->generation, NEARBY_EVENT_MESSAGE_SENT,
                   (int32_t)work->id, 0, (int32_t)size);
    } else {
        emit_error(work->generation, work->id, NEARBY_ERROR_TRANSPORT);
    }
}

static void process_send_blob_offer(const nearby_work_t *work)
{
    uint8_t metadata[NEARBY_BLOB_METADATA_FIXED +
                     NEARBY_BLOB_NAME_MAX + NEARBY_BLOB_MIME_MAX];
    size_t name_length = strlen(work->name);
    size_t mime_length = strlen(work->mime);
    uint32_t size;
    uint8_t digest[32];

    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool retained = find_buffer_locked(work->handle, false, NULL) != NULL ||
                    object_matches_locked(work->handle, false);
    if (find_buffer_locked(work->handle, false, NULL)) {
        size = find_buffer_locked(work->handle, false, NULL)->length;
    } else {
        size = s_nearby.object.size;
    }
    xSemaphoreGive(s_nearby.mutex);
    if (!retained || !handle_digest(work->handle, digest)) {
        finish_outgoing(work->generation, NEARBY_EVENT_BLOB_SENT,
                        NEARBY_ERROR_PROTOCOL);
        return;
    }
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (s_nearby.outgoing.active && s_nearby.outgoing.id == work->id) {
        memcpy(s_nearby.outgoing.digest, digest, sizeof(digest));
    }
    xSemaphoreGive(s_nearby.mutex);
    memcpy(metadata, digest, 32U);
    metadata[32U] = (uint8_t)name_length;
    metadata[33U] = (uint8_t)mime_length;
    memcpy(metadata + NEARBY_BLOB_METADATA_FIXED, work->name, name_length);
    memcpy(metadata + NEARBY_BLOB_METADATA_FIXED + name_length,
           work->mime, mime_length);
    if (!send_frame(work->generation, NEARBY_FRAME_BLOB_OFFER,
                    NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST,
                    work->id, 0U, size, metadata,
                    NEARBY_BLOB_METADATA_FIXED + name_length + mime_length)) {
        finish_outgoing(work->generation, NEARBY_EVENT_BLOB_SENT,
                        NEARBY_ERROR_TRANSPORT);
    }
}

static void process_accept_blob(const nearby_work_t *work)
{
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = s_nearby.incoming.pending &&
                 s_nearby.incoming.id == work->id &&
                 !s_nearby.outgoing.active && s_nearby.object.busy == 0U;
    if (valid) memset(&s_nearby.object, 0, sizeof(s_nearby.object));
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) return;
    esp_err_t result = esp_partition_erase_range(
        s_nearby.partition, 0U, s_nearby.partition->size);
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    valid = result == ESP_OK && s_nearby.incoming.pending &&
            s_nearby.incoming.id == work->id &&
            s_nearby.generation == work->generation;
    if (valid) {
        s_nearby.incoming.pending = false;
        s_nearby.incoming.receiving = true;
        s_nearby.incoming.received = 0U;
        mbedtls_sha256_init(&s_nearby.incoming.sha);
        valid = mbedtls_sha256_starts(&s_nearby.incoming.sha, 0) == 0;
        s_nearby.incoming.sha_active = valid;
        if (!valid) reset_incoming_locked();
    }
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) {
        emit_error(work->generation, work->id, NEARBY_ERROR_FLASH);
        return;
    }
    if (!send_frame(work->generation, NEARBY_FRAME_BLOB_DECISION,
                    NEARBY_FRAME_FLAG_ACCEPT, work->id, 0U, 0U, NULL, 0U)) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        if (s_nearby.incoming.id == work->id) reset_incoming_locked();
        xSemaphoreGive(s_nearby.mutex);
        emit_error(work->generation, work->id, NEARBY_ERROR_TRANSPORT);
    }
}

static void process_reject_blob(const nearby_work_t *work)
{
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = s_nearby.incoming.pending &&
                 s_nearby.incoming.id == work->id;
    if (valid) reset_incoming_locked();
    xSemaphoreGive(s_nearby.mutex);
    if (valid) {
        if (!send_frame(work->generation, NEARBY_FRAME_BLOB_DECISION, 0U,
                        work->id, 0U, 0U, NULL, 0U)) {
            emit_error(work->generation, work->id, NEARBY_ERROR_TRANSPORT);
        }
    }
}

static void work_task(void *argument)
{
    nearby_work_t work;
    (void)argument;
    for (;;) {
        if (xQueueReceive(s_nearby.work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        bool current = s_nearby.foreground_owner != 0U &&
                       s_nearby.generation == work.generation;
        xSemaphoreGive(s_nearby.mutex);
        if (!current) continue;
        switch (work.type) {
        case WORK_FRAME:
            process_frame(work.generation, work.data, work.size);
            break;
        case WORK_STATE:
            handle_state(work.generation, work.state);
            break;
        case WORK_SEND_MESSAGE:
            process_send_message(&work);
            break;
        case WORK_SEND_BLOB_OFFER:
            process_send_blob_offer(&work);
            break;
        case WORK_ACCEPT_BLOB:
            process_accept_blob(&work);
            break;
        case WORK_REJECT_BLOB:
            process_reject_blob(&work);
            break;
        }
    }
}

static void voice_task(void *argument)
{
    bool audio_session = false;
    int16_t pcm[NEARBY_VOICE_SAMPLES];
    uint8_t encoded[NEARBY_VOICE_BLOCK_SIZE];
    voice_packet_t packet;
    (void)argument;

    for (;;) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        bool active = s_nearby.voice_active;
        bool transmitting = s_nearby.voice_transmitting;
        uint32_t generation = s_nearby.generation;
        uint32_t sequence = s_nearby.voice_sequence;
        xSemaphoreGive(s_nearby.mutex);

        if (!active) {
            if (audio_session) {
                bsp_audio_session_end();
                audio_session = false;
            }
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }
        if (!audio_session) {
            bool acquired = bsp_audio_session_begin(1000U);
            if (!acquired ||
                bsp_audio_set_format(NEARBY_VOICE_SAMPLE_RATE, 16U, 1U) != ESP_OK) {
                if (acquired) bsp_audio_session_end();
                xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
                if (s_nearby.generation == generation) {
                    s_nearby.voice_active = false;
                    s_nearby.voice_transmitting = false;
                }
                xSemaphoreGive(s_nearby.mutex);
                emit_error(generation, 0U, NEARBY_ERROR_AUDIO);
                continue;
            }
            audio_session = true;
        }
        if (transmitting) {
            if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK ||
                nearby_adpcm_encode(pcm, encoded) != sizeof(encoded) ||
                !send_frame(generation, NEARBY_FRAME_VOICE,
                            NEARBY_FRAME_FLAG_FIRST | NEARBY_FRAME_FLAG_LAST,
                            generation, sequence, NEARBY_VOICE_SAMPLE_RATE,
                            encoded, sizeof(encoded))) {
                emit_error(generation, 0U, NEARBY_ERROR_TRANSPORT);
                vTaskDelay(pdMS_TO_TICKS(20U));
            }
            xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
            if (s_nearby.generation == generation) ++s_nearby.voice_sequence;
            xSemaphoreGive(s_nearby.mutex);
        } else if (xQueueReceive(s_nearby.voice_queue, &packet,
                                 pdMS_TO_TICKS(20U)) == pdTRUE &&
                   packet.generation == generation &&
                   nearby_adpcm_decode(packet.data, pcm)) {
            if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) {
                emit_error(generation, 0U, NEARBY_ERROR_AUDIO);
            }
        }
    }
}

static void ble_state_callback(void *context, plugin_ble_sync_state_t state)
{
    nearby_work_t work = { .type = WORK_STATE, .state = state };
    (void)context;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    work.generation = s_nearby.generation;
    bool active = s_nearby.foreground_owner != 0U && s_nearby.lease;
    xSemaphoreGive(s_nearby.mutex);
    if (active) xQueueSend(s_nearby.work_queue, &work, 0);
}

static bool ble_frame_callback(void *context, const uint8_t *data, size_t size)
{
    nearby_work_t work = { .type = WORK_FRAME, .size = (uint16_t)size };
    (void)context;
    if (!data || size == 0U || size > sizeof(work.data)) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    work.generation = s_nearby.generation;
    bool active = s_nearby.foreground_owner != 0U && s_nearby.lease;
    xSemaphoreGive(s_nearby.mutex);
    if (!active) return false;
    memcpy(work.data, data, size);
    return xQueueSend(s_nearby.work_queue, &work, 0) == pdTRUE;
}

esp_err_t nearby_service_system_init(bool audio_available)
{
    if (s_nearby.initialized) return ESP_OK;
    memset(&s_nearby, 0, sizeof(s_nearby));
    s_nearby.mutex = xSemaphoreCreateMutex();
    s_nearby.tx_mutex = xSemaphoreCreateMutex();
    s_nearby.work_queue = xQueueCreate(NEARBY_WORK_DEPTH, sizeof(nearby_work_t));
    s_nearby.event_queue = xQueueCreate(NEARBY_EVENT_DEPTH, sizeof(nearby_event_t));
    s_nearby.voice_queue = xQueueCreate(NEARBY_VOICE_QUEUE_DEPTH,
                                        sizeof(voice_packet_t));
    if (!s_nearby.mutex || !s_nearby.tx_mutex || !s_nearby.work_queue ||
        !s_nearby.event_queue || !s_nearby.voice_queue) {
        return ESP_ERR_NO_MEM;
    }
    s_nearby.partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, NEARBY_PARTITION_SUBTYPE,
        NEARBY_PARTITION_LABEL);
    if (!s_nearby.partition) return ESP_ERR_NOT_FOUND;
    if (xTaskCreate(work_task, "nearby_work", 5120U, NULL, 5U, NULL) != pdPASS ||
        xTaskCreate(voice_task, "nearby_voice", 4096U, NULL, 6U, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_nearby.audio_available = audio_available;
    s_nearby.generation = 1U;
    s_nearby.next_message_id = 1U;
    s_nearby.next_transfer_id = 1U;
    s_nearby.initialized = true;
    ESP_LOGI(TAG, "system service ready: blob capacity=%u audio=%d",
             (unsigned)s_nearby.partition->size, audio_available);
    return ESP_OK;
}

static void drain_queue(QueueHandle_t queue, size_t item_size)
{
    uint8_t storage[sizeof(nearby_work_t)];
    if (!queue || item_size > sizeof(storage)) return;
    while (xQueueReceive(queue, storage, 0) == pdTRUE) {
    }
}

static void drain_event_queue(void)
{
    nearby_event_t event;
    while (xQueueReceive(s_nearby.event_queue, &event, 0) == pdTRUE) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        discard_event_locked(&event);
        xSemaphoreGive(s_nearby.mutex);
    }
}

esp_err_t nearby_service_foreground_enter(uint32_t owner)
{
    if (!s_nearby.initialized || owner == 0U) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (s_nearby.foreground_owner != 0U) {
        xSemaphoreGive(s_nearby.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_nearby.generation = next_nonzero(s_nearby.generation);
    s_nearby.foreground_owner = owner;
    s_nearby.lease = false;
    memset(s_nearby.buffers, 0, sizeof(s_nearby.buffers));
    memset(&s_nearby.object, 0, sizeof(s_nearby.object));
    memset(&s_nearby.incoming, 0, sizeof(s_nearby.incoming));
    memset(&s_nearby.outgoing, 0, sizeof(s_nearby.outgoing));
    reset_message_locked();
    s_nearby.voice_active = false;
    s_nearby.voice_transmitting = false;
    xSemaphoreGive(s_nearby.mutex);
    drain_queue(s_nearby.work_queue, sizeof(nearby_work_t));
    drain_event_queue();
    drain_queue(s_nearby.voice_queue, sizeof(voice_packet_t));
    return ESP_OK;
}

void nearby_service_foreground_exit(uint32_t owner)
{
    if (!s_nearby.initialized || owner == 0U) return;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (!owner_matches_locked(owner)) {
        xSemaphoreGive(s_nearby.mutex);
        return;
    }
    bool stop_ble = s_nearby.lease;
    s_nearby.generation = next_nonzero(s_nearby.generation);
    s_nearby.lease = false;
    s_nearby.voice_active = false;
    s_nearby.voice_transmitting = false;
    reset_message_locked();
    reset_incoming_locked();
    reset_outgoing_locked();
    memset(s_nearby.buffers, 0, sizeof(s_nearby.buffers));
    memset(&s_nearby.object, 0, sizeof(s_nearby.object));
    s_nearby.foreground_owner = 0U;
    xSemaphoreGive(s_nearby.mutex);
    if (stop_ble) plugin_ble_stop_runtime();
    drain_queue(s_nearby.work_queue, sizeof(nearby_work_t));
    drain_event_queue();
    drain_queue(s_nearby.voice_queue, sizeof(voice_packet_t));
    ESP_LOGI(TAG, "foreground resources released");
}

bool nearby_service_acquire(uint32_t owner)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (!owner_matches_locked(owner)) {
        xSemaphoreGive(s_nearby.mutex);
        return false;
    }
    if (s_nearby.lease) {
        xSemaphoreGive(s_nearby.mutex);
        return true;
    }
    s_nearby.lease = true;
    uint32_t generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);

    const plugin_ble_runtime_callbacks_t callbacks = {
        .state = ble_state_callback,
        .frame = ble_frame_callback,
    };
    if (plugin_ble_start_runtime(&callbacks, NULL) != ESP_OK) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        if (s_nearby.generation == generation) s_nearby.lease = false;
        xSemaphoreGive(s_nearby.mutex);
        return false;
    }
    emit_event(generation, NEARBY_EVENT_STATE, 0, 0,
               PLUGIN_BLE_DISCONNECTED);
    return true;
}

bool nearby_service_release(uint32_t owner)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    if (!owner_matches_locked(owner)) {
        xSemaphoreGive(s_nearby.mutex);
        return false;
    }
    bool stop_ble = s_nearby.lease;
    s_nearby.lease = false;
    s_nearby.generation = next_nonzero(s_nearby.generation);
    s_nearby.voice_active = false;
    s_nearby.voice_transmitting = false;
    reset_message_locked();
    reset_incoming_locked();
    reset_outgoing_locked();
    for (size_t index = 0; index < NEARBY_BUFFER_COUNT; ++index) {
        s_nearby.buffers[index].busy = 0U;
        if (s_nearby.buffers[index].used &&
            !s_nearby.buffers[index].plugin_owned) {
            memset(&s_nearby.buffers[index], 0, sizeof(s_nearby.buffers[index]));
        }
    }
    s_nearby.object.busy = 0U;
    if (s_nearby.object.valid && !s_nearby.object.plugin_owned) {
        memset(&s_nearby.object, 0, sizeof(s_nearby.object));
    }
    xSemaphoreGive(s_nearby.mutex);
    if (stop_ble) plugin_ble_stop_runtime();
    drain_queue(s_nearby.work_queue, sizeof(nearby_work_t));
    drain_event_queue();
    drain_queue(s_nearby.voice_queue, sizeof(voice_packet_t));
    return true;
}

bool nearby_service_poll(uint32_t owner, nearby_event_t *event)
{
    if (!s_nearby.initialized || !event) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = owner_matches_locked(owner);
    xSemaphoreGive(s_nearby.mutex);
    return valid && xQueueReceive(s_nearby.event_queue, event, 0) == pdTRUE;
}

bool nearby_service_buffer_alloc(uint32_t owner, uint16_t capacity,
                                 int32_t *handle)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool result = owner_matches_locked(owner) &&
                  allocate_buffer_locked(capacity, handle);
    xSemaphoreGive(s_nearby.mutex);
    return result;
}

bool nearby_service_buffer_release(uint32_t owner, int32_t handle)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = find_buffer_locked(handle, true, NULL);
    bool object = object_matches_locked(handle, true);
    bool valid = owner_matches_locked(owner) && (buffer || object);
    if (valid) release_handle_locked(handle);
    xSemaphoreGive(s_nearby.mutex);
    return valid;
}

bool nearby_service_buffer_length(uint32_t owner, int32_t handle,
                                  int32_t *length)
{
    if (!s_nearby.initialized || !length) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = owner_matches_locked(owner);
    nearby_buffer_t *buffer = valid ? find_buffer_locked(handle, true, NULL) : NULL;
    if (buffer) *length = buffer->length;
    else if (valid && object_matches_locked(handle, true)) {
        *length = (int32_t)s_nearby.object.size;
    } else valid = false;
    xSemaphoreGive(s_nearby.mutex);
    return valid;
}

bool nearby_service_buffer_read_u8(uint32_t owner, int32_t handle,
                                   int32_t index, int32_t *value)
{
    if (!s_nearby.initialized || !value || index < 0) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = owner_matches_locked(owner);
    nearby_buffer_t *buffer = valid ? find_buffer_locked(handle, true, NULL) : NULL;
    if (buffer && (uint32_t)index < buffer->length) {
        *value = buffer->data[index];
    } else if (valid && object_matches_locked(handle, true) &&
               (uint32_t)index < s_nearby.object.size) {
        uint8_t byte;
        valid = esp_partition_read(s_nearby.partition, (uint32_t)index,
                                   &byte, 1U) == ESP_OK;
        if (valid) *value = byte;
    } else valid = false;
    xSemaphoreGive(s_nearby.mutex);
    return valid;
}

bool nearby_service_buffer_write_u8(uint32_t owner, int32_t handle,
                                    int32_t index, int32_t value)
{
    if (!s_nearby.initialized || index < 0 || value < 0 || value > 255) {
        return false;
    }
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = owner_matches_locked(owner) ?
        find_buffer_locked(handle, true, NULL) : NULL;
    bool valid = buffer && (uint32_t)index < buffer->capacity;
    if (valid) {
        buffer->data[index] = (uint8_t)value;
        if ((uint32_t)index >= buffer->length) buffer->length = index + 1;
    }
    xSemaphoreGive(s_nearby.mutex);
    return valid;
}

bool nearby_service_buffer_append_text(uint32_t owner, int32_t handle,
                                       const char *text)
{
    if (!s_nearby.initialized || !text) return false;
    size_t size = strlen(text);
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    nearby_buffer_t *buffer = owner_matches_locked(owner) ?
        find_buffer_locked(handle, true, NULL) : NULL;
    bool valid = buffer && size <= buffer->capacity - buffer->length;
    if (valid) {
        memcpy(buffer->data + buffer->length, text, size);
        buffer->length += size;
    }
    xSemaphoreGive(s_nearby.mutex);
    return valid;
}

static bool ready_to_send_locked(uint32_t owner)
{
    return owner_matches_locked(owner) && s_nearby.lease &&
           plugin_ble_sync_state() == PLUGIN_BLE_SYNCED;
}

bool nearby_service_send(uint32_t owner, int32_t handle, int32_t *message_id)
{
    if (!s_nearby.initialized || !message_id) return false;
    nearby_work_t work = { .type = WORK_SEND_MESSAGE, .handle = handle };
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    uint32_t size = 0U;
    bool valid = ready_to_send_locked(owner) &&
                 find_buffer_locked(handle, true, NULL) != NULL &&
                 retain_handle_locked(handle, &size);
    if (valid) {
        s_nearby.next_message_id = next_nonzero(s_nearby.next_message_id);
        work.id = s_nearby.next_message_id;
        work.generation = s_nearby.generation;
        *message_id = (int32_t)work.id;
    }
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) return false;
    if (xQueueSend(s_nearby.work_queue, &work, 0) != pdTRUE) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        unretain_handle_locked(handle);
        xSemaphoreGive(s_nearby.mutex);
        return false;
    }
    return true;
}

bool nearby_service_blob_accept(uint32_t owner, int32_t transfer_id)
{
    nearby_work_t work = { .type = WORK_ACCEPT_BLOB, .id = (uint32_t)transfer_id };
    if (!s_nearby.initialized || transfer_id <= 0) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = ready_to_send_locked(owner) && s_nearby.incoming.pending &&
                 s_nearby.incoming.id == (uint32_t)transfer_id &&
                 !s_nearby.outgoing.active && s_nearby.object.busy == 0U;
    work.generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);
    return valid && xQueueSend(s_nearby.work_queue, &work, 0) == pdTRUE;
}

bool nearby_service_blob_reject(uint32_t owner, int32_t transfer_id)
{
    nearby_work_t work = { .type = WORK_REJECT_BLOB, .id = (uint32_t)transfer_id };
    if (!s_nearby.initialized || transfer_id <= 0) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = ready_to_send_locked(owner) && s_nearby.incoming.pending &&
                 s_nearby.incoming.id == (uint32_t)transfer_id;
    work.generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);
    return valid && xQueueSend(s_nearby.work_queue, &work, 0) == pdTRUE;
}

bool nearby_service_blob_send(uint32_t owner, int32_t handle,
                              const char *name, const char *mime,
                              int32_t *transfer_id)
{
    if (!s_nearby.initialized || !name || !mime || !transfer_id ||
        strlen(name) == 0U || strlen(name) > NEARBY_BLOB_NAME_MAX ||
        strlen(mime) == 0U || strlen(mime) > NEARBY_BLOB_MIME_MAX) {
        return false;
    }
    nearby_work_t work = { .type = WORK_SEND_BLOB_OFFER, .handle = handle };
    snprintf(work.name, sizeof(work.name), "%s", name);
    snprintf(work.mime, sizeof(work.mime), "%s", mime);
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    uint32_t size = 0U;
    bool retained = false;
    if (ready_to_send_locked(owner) && !s_nearby.outgoing.active &&
        !s_nearby.incoming.pending && !s_nearby.incoming.receiving) {
        retained = retain_handle_locked(handle, &size);
    }
    bool valid = retained && size > 0U;
    if (valid) {
        s_nearby.next_transfer_id = next_nonzero(s_nearby.next_transfer_id);
        work.id = s_nearby.next_transfer_id;
        work.generation = s_nearby.generation;
        s_nearby.outgoing.active = true;
        s_nearby.outgoing.id = work.id;
        s_nearby.outgoing.handle = handle;
        s_nearby.outgoing.size = size;
        snprintf(s_nearby.outgoing.name, sizeof(s_nearby.outgoing.name), "%s", name);
        snprintf(s_nearby.outgoing.mime, sizeof(s_nearby.outgoing.mime), "%s", mime);
        *transfer_id = (int32_t)work.id;
    } else if (retained) {
        unretain_handle_locked(handle);
    }
    xSemaphoreGive(s_nearby.mutex);
    if (!valid) return false;
    if (xQueueSend(s_nearby.work_queue, &work, 0) != pdTRUE) {
        xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
        reset_outgoing_locked();
        xSemaphoreGive(s_nearby.mutex);
        return false;
    }
    return true;
}

bool nearby_service_voice_start(uint32_t owner)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = ready_to_send_locked(owner) && s_nearby.audio_available;
    if (valid) {
        s_nearby.voice_active = true;
        s_nearby.voice_transmitting = false;
        s_nearby.voice_sequence = 0U;
    }
    uint32_t generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);
    if (valid) {
        emit_event(generation, NEARBY_EVENT_VOICE_STATE, 0, 0,
                   NEARBY_VOICE_LISTENING);
    }
    return valid;
}

bool nearby_service_voice_transmit(uint32_t owner, bool enabled)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = owner_matches_locked(owner) && s_nearby.voice_active;
    if (valid) s_nearby.voice_transmitting = enabled;
    uint32_t generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);
    if (valid) {
        emit_event(generation, NEARBY_EVENT_VOICE_STATE, 0, 0,
                   enabled ? NEARBY_VOICE_TRANSMITTING :
                             NEARBY_VOICE_LISTENING);
    }
    return valid;
}

bool nearby_service_voice_stop(uint32_t owner)
{
    if (!s_nearby.initialized) return false;
    xSemaphoreTake(s_nearby.mutex, portMAX_DELAY);
    bool valid = owner_matches_locked(owner);
    if (valid) {
        s_nearby.voice_active = false;
        s_nearby.voice_transmitting = false;
    }
    uint32_t generation = s_nearby.generation;
    xSemaphoreGive(s_nearby.mutex);
    if (valid) {
        emit_event(generation, NEARBY_EVENT_VOICE_STATE, 0, 0,
                   NEARBY_VOICE_STOPPED);
    }
    return valid;
}
