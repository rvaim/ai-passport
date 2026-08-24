#pragma once

#include "plugin_format.h"
#include "plugin_store.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PLUGIN_INSTALL_IDLE = 0,
    PLUGIN_INSTALL_RECEIVING,
    PLUGIN_INSTALL_VERIFYING,
    PLUGIN_INSTALL_WAITING_APPROVAL,
    PLUGIN_INSTALL_INSTALLING,
    PLUGIN_INSTALL_COMPLETE,
    PLUGIN_INSTALL_ERROR,
} plugin_install_state_t;

typedef struct {
    plugin_install_state_t state;
    esp_err_t error;
    size_t received;
    size_t expected;
    plugin_manifest_t pending;
    plugin_record_t installed;
} plugin_installer_snapshot_t;

esp_err_t plugin_installer_init(void);
esp_err_t plugin_installer_begin(size_t total_size);
esp_err_t plugin_installer_write(size_t offset, const void *data, size_t size);
esp_err_t plugin_installer_finish(void);
esp_err_t plugin_installer_approve(void);
void plugin_installer_reject(void);
void plugin_installer_reset(void);
void plugin_installer_snapshot(plugin_installer_snapshot_t *snapshot);
