#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PLUGIN_BLE_DISCONNECTED = 0,
    PLUGIN_BLE_CONNECTED,
    PLUGIN_BLE_SYNCED,
    PLUGIN_BLE_CODE_MISMATCH,
} plugin_ble_sync_state_t;

typedef void (*plugin_ble_runtime_state_callback_t)(
    void *context, plugin_ble_sync_state_t state);
typedef bool (*plugin_ble_runtime_frame_callback_t)(
    void *context, const uint8_t *data, size_t size);

typedef struct {
    plugin_ble_runtime_state_callback_t state;
    plugin_ble_runtime_frame_callback_t frame;
} plugin_ble_runtime_callbacks_t;

/*
 * Unpaired BLE is only a transport. The browser must first submit the fixed
 * per-device code; package trust still comes from its signature and physical OK.
 */
esp_err_t plugin_ble_start(void);
esp_err_t plugin_ble_start_runtime(const plugin_ble_runtime_callbacks_t *callbacks,
                                   void *context);
void plugin_ble_stop_installer(void);
void plugin_ble_stop_runtime(void);
bool plugin_ble_running(void);
bool plugin_ble_installer_running(void);
bool plugin_ble_runtime_running(void);
plugin_ble_sync_state_t plugin_ble_sync_state(void);
bool plugin_ble_runtime_send(const uint8_t *data, size_t size);
