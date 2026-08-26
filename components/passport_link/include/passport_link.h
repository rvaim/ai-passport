#pragma once

#include "esp_err.h"
#include "passport_link_protocol.h"
#include "passport_package.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*passport_link_rx_cb_t)(const passport_link_frame_t *frame, void *user);
typedef void (*passport_link_install_cb_t)(esp_err_t result, const passport_package_result_t *package, void *user);

/** Start the unpaired BLE service and advertise the public Passport device code. */
esp_err_t passport_link_init(void);

/** Whether a BLE peer is currently connected and subscribed to outgoing notifications. */
bool passport_link_connected(void);

/** Register the foreground-app message receiver. Only target-matched, CRC-valid frames are delivered. */
void passport_link_set_rx_callback(passport_link_rx_cb_t cb, void *user);

/** Observe successful/failed BLE package installation events. */
void passport_link_set_install_callback(passport_link_install_cb_t cb, void *user);

/**
 * Send one frame to the currently connected peer. target_id is explicit to prevent accidental routing.
 * V1 does not actively scan/connect to a remote Passport; mobile/peer clients connect to this GATT server.
 */
esp_err_t passport_link_send(uint64_t target_id, const char *service_name, uint8_t type,
                             const void *payload, size_t payload_len);
