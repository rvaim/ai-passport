#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEARBY_BUFFER_COUNT 4U
#define NEARBY_BUFFER_CAPACITY_MAX 4096U

typedef enum {
    NEARBY_EVENT_STATE = 1,
    NEARBY_EVENT_MESSAGE,
    NEARBY_EVENT_MESSAGE_SENT,
    NEARBY_EVENT_BLOB_OFFER,
    NEARBY_EVENT_BLOB_PROGRESS,
    NEARBY_EVENT_BLOB_READY,
    NEARBY_EVENT_BLOB_SENT,
    NEARBY_EVENT_BLOB_REJECTED,
    NEARBY_EVENT_VOICE_STATE,
    NEARBY_EVENT_ERROR,
} nearby_event_type_t;

typedef enum {
    NEARBY_VOICE_STOPPED = 0,
    NEARBY_VOICE_LISTENING,
    NEARBY_VOICE_TRANSMITTING,
} nearby_voice_state_t;

typedef enum {
    NEARBY_ERROR_PROTOCOL = 1,
    NEARBY_ERROR_TOO_LARGE,
    NEARBY_ERROR_NO_BUFFER,
    NEARBY_ERROR_OUT_OF_ORDER,
    NEARBY_ERROR_FLASH,
    NEARBY_ERROR_DIGEST,
    NEARBY_ERROR_TRANSPORT,
    NEARBY_ERROR_BUSY,
    NEARBY_ERROR_AUDIO,
} nearby_error_t;

typedef struct {
    int32_t type;
    int32_t id;
    int32_t handle;
    int32_t value;
} nearby_event_t;

esp_err_t nearby_service_system_init(bool audio_available);
esp_err_t nearby_service_foreground_enter(uint32_t owner);
void nearby_service_foreground_exit(uint32_t owner);

bool nearby_service_acquire(uint32_t owner);
bool nearby_service_release(uint32_t owner);
bool nearby_service_poll(uint32_t owner, nearby_event_t *event);

bool nearby_service_buffer_alloc(uint32_t owner, uint16_t capacity,
                                 int32_t *handle);
bool nearby_service_buffer_release(uint32_t owner, int32_t handle);
bool nearby_service_buffer_length(uint32_t owner, int32_t handle,
                                  int32_t *length);
bool nearby_service_buffer_read_u8(uint32_t owner, int32_t handle,
                                   int32_t index, int32_t *value);
bool nearby_service_buffer_write_u8(uint32_t owner, int32_t handle,
                                    int32_t index, int32_t value);
bool nearby_service_buffer_append_text(uint32_t owner, int32_t handle,
                                       const char *text);

bool nearby_service_send(uint32_t owner, int32_t handle, int32_t *message_id);
bool nearby_service_blob_accept(uint32_t owner, int32_t transfer_id);
bool nearby_service_blob_reject(uint32_t owner, int32_t transfer_id);
bool nearby_service_blob_send(uint32_t owner, int32_t handle,
                              const char *name, const char *mime,
                              int32_t *transfer_id);

bool nearby_service_voice_start(uint32_t owner);
bool nearby_service_voice_transmit(uint32_t owner, bool enabled);
bool nearby_service_voice_stop(uint32_t owner);
