#include "plugin_installer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

typedef enum {
    WORK_VERIFY,
    WORK_INSTALL,
} work_type_t;

typedef struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    plugin_installer_snapshot_t snapshot;
    bool initialized;
} installer_state_t;

#define INSTALLER_WORKER_STACK_SIZE 6144U

static installer_state_t s_installer;

static void set_error(esp_err_t error)
{
    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    s_installer.snapshot.state = PLUGIN_INSTALL_ERROR;
    s_installer.snapshot.error = error;
    xSemaphoreGive(s_installer.mutex);
}

static void worker(void *argument)
{
    work_type_t work;
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_installer.queue, &work, portMAX_DELAY) != pdTRUE) continue;
        if (work == WORK_VERIFY) {
            plugin_manifest_t manifest;
            esp_err_t result = plugin_store_stage_finish(&manifest);
            if (result != ESP_OK) {
                set_error(result);
                continue;
            }
            xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
            s_installer.snapshot.pending = manifest;
            s_installer.snapshot.state = PLUGIN_INSTALL_WAITING_APPROVAL;
            s_installer.snapshot.error = ESP_OK;
            xSemaphoreGive(s_installer.mutex);
        } else if (work == WORK_INSTALL) {
            plugin_record_t installed;
            esp_err_t result = plugin_store_commit_staged(&installed);
            if (result != ESP_OK) {
                set_error(result);
                continue;
            }
            xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
            s_installer.snapshot.installed = installed;
            s_installer.snapshot.state = PLUGIN_INSTALL_COMPLETE;
            s_installer.snapshot.error = ESP_OK;
            xSemaphoreGive(s_installer.mutex);
        }
    }
}

esp_err_t plugin_installer_init(void)
{
    if (s_installer.initialized) return ESP_OK;
    esp_err_t result = plugin_store_init();
    if (result != ESP_OK) return result;

    s_installer.mutex = xSemaphoreCreateMutex();
    s_installer.queue = xQueueCreate(2U, sizeof(work_type_t));
    if (!s_installer.mutex || !s_installer.queue) {
        if (s_installer.queue) vQueueDelete(s_installer.queue);
        if (s_installer.mutex) vSemaphoreDelete(s_installer.mutex);
        s_installer.queue = NULL;
        s_installer.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_installer.snapshot.state = PLUGIN_INSTALL_IDLE;
    // Signature verification is the deepest remaining call chain. The store's
    // bulk workspaces live outside this stack, and this size keeps explicit
    // headroom for the crypto backend and future manifest fields.
    if (xTaskCreate(worker, "plugin_install", INSTALLER_WORKER_STACK_SIZE,
                    NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_installer.queue);
        vSemaphoreDelete(s_installer.mutex);
        s_installer.queue = NULL;
        s_installer.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_installer.initialized = true;
    return ESP_OK;
}

esp_err_t plugin_installer_begin(size_t total_size)
{
    esp_err_t result;
    if (!s_installer.initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    plugin_install_state_t state = s_installer.snapshot.state;
    xSemaphoreGive(s_installer.mutex);
    if (state == PLUGIN_INSTALL_VERIFYING || state == PLUGIN_INSTALL_INSTALLING ||
        state == PLUGIN_INSTALL_WAITING_APPROVAL) {
        return ESP_ERR_INVALID_STATE;
    }
    result = plugin_store_stage_begin(total_size);
    if (result != ESP_OK) {
        set_error(result);
        return result;
    }

    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    memset(&s_installer.snapshot, 0, sizeof(s_installer.snapshot));
    s_installer.snapshot.state = PLUGIN_INSTALL_RECEIVING;
    s_installer.snapshot.expected = total_size;
    xSemaphoreGive(s_installer.mutex);
    return ESP_OK;
}

esp_err_t plugin_installer_write(size_t offset, const void *data, size_t size)
{
    esp_err_t result;
    if (!s_installer.initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    bool receiving = s_installer.snapshot.state == PLUGIN_INSTALL_RECEIVING;
    xSemaphoreGive(s_installer.mutex);
    if (!receiving) return ESP_ERR_INVALID_STATE;

    result = plugin_store_stage_write(offset, data, size);
    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    s_installer.snapshot.received = plugin_store_stage_received();
    if (result != ESP_OK) {
        s_installer.snapshot.state = PLUGIN_INSTALL_ERROR;
        s_installer.snapshot.error = result;
    }
    xSemaphoreGive(s_installer.mutex);
    return result;
}

esp_err_t plugin_installer_finish(void)
{
    work_type_t work = WORK_VERIFY;
    if (!s_installer.initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    if (s_installer.snapshot.state != PLUGIN_INSTALL_RECEIVING ||
        s_installer.snapshot.received != s_installer.snapshot.expected) {
        xSemaphoreGive(s_installer.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_installer.snapshot.state = PLUGIN_INSTALL_VERIFYING;
    xSemaphoreGive(s_installer.mutex);
    if (xQueueSend(s_installer.queue, &work, 0) != pdTRUE) {
        set_error(ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t plugin_installer_approve(void)
{
    work_type_t work = WORK_INSTALL;
    if (!s_installer.initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    if (s_installer.snapshot.state != PLUGIN_INSTALL_WAITING_APPROVAL) {
        xSemaphoreGive(s_installer.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_installer.snapshot.state = PLUGIN_INSTALL_INSTALLING;
    xSemaphoreGive(s_installer.mutex);
    if (xQueueSend(s_installer.queue, &work, 0) != pdTRUE) {
        set_error(ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void plugin_installer_reject(void)
{
    if (!s_installer.initialized) return;
    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    if (s_installer.snapshot.state == PLUGIN_INSTALL_WAITING_APPROVAL) {
        memset(&s_installer.snapshot, 0, sizeof(s_installer.snapshot));
        s_installer.snapshot.state = PLUGIN_INSTALL_IDLE;
        plugin_store_stage_abort();
    }
    xSemaphoreGive(s_installer.mutex);
}

void plugin_installer_reset(void)
{
    if (!s_installer.initialized) return;
    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    if (s_installer.snapshot.state != PLUGIN_INSTALL_VERIFYING &&
        s_installer.snapshot.state != PLUGIN_INSTALL_INSTALLING) {
        memset(&s_installer.snapshot, 0, sizeof(s_installer.snapshot));
        s_installer.snapshot.state = PLUGIN_INSTALL_IDLE;
        plugin_store_stage_abort();
    }
    xSemaphoreGive(s_installer.mutex);
}

void plugin_installer_snapshot(plugin_installer_snapshot_t *snapshot)
{
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_installer.initialized) return;
    xSemaphoreTake(s_installer.mutex, portMAX_DELAY);
    *snapshot = s_installer.snapshot;
    xSemaphoreGive(s_installer.mutex);
}
