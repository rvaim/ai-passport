#include "plugin_ble.h"

#include "device_code.h"
#include "device_identity.h"
#include "plugin_installer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdio.h>
#include <string.h>

#define BLE_WORK_QUEUE_DEPTH 8U
#define BLE_DATA_MAX 249U
#define BLE_ATTRIBUTE_MAX 253U
#define BLE_STATUS_VERSION 2U
#define BLE_RUNTIME_STATUS_VERSION 1U
#define BLE_CONTROL_SYNC 0x10U

typedef enum {
    BLE_MODE_NONE = 0,
    BLE_MODE_INSTALLER,
    BLE_MODE_RUNTIME,
} ble_mode_t;

typedef enum {
    BLE_CH_INSTALL_CONTROL = 1,
    BLE_CH_INSTALL_DATA,
    BLE_CH_INSTALL_STATUS,
    BLE_CH_RUNTIME_CONTROL = 11,
    BLE_CH_RUNTIME_RX,
    BLE_CH_RUNTIME_TX,
    BLE_CH_RUNTIME_STATUS,
} ble_characteristic_t;

typedef enum {
    BLE_WORK_BEGIN,
    BLE_WORK_DATA,
    BLE_WORK_FINISH,
    BLE_WORK_ABORT,
    BLE_WORK_RESET,
} ble_work_type_t;

typedef struct {
    ble_work_type_t type;
    uint16_t connection;
    uint32_t session_generation;
    bool session_bound;
    uint32_t offset;
    uint32_t total_size;
    uint16_t size;
    uint8_t data[BLE_DATA_MAX];
} ble_work_t;

typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t lifecycle_mutex;
    QueueHandle_t queue;
    uint16_t connection;
    uint16_t status_handle;
    uint16_t runtime_tx_handle;
    uint16_t runtime_status_handle;
    uint32_t session_generation;
    uint8_t own_address_type;
    plugin_ble_sync_state_t sync_state;
    ble_mode_t mode;
    plugin_ble_runtime_callbacks_t runtime_callbacks;
    void *runtime_context;
    bool worker_ready;
    bool stack_ready;
    bool running;
    bool runtime_subscribed;
} ble_state_t;

static const char *TAG = "plugin_ble";
static ble_state_t s_ble = {
    .connection = BLE_HS_CONN_HANDLE_NONE,
};
static portMUX_TYPE s_worker_init_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_worker_initializing;
static char s_device_name[24] = "Passport-Plugin";
static uint8_t s_control_characteristic = BLE_CH_INSTALL_CONTROL;
static uint8_t s_data_characteristic = BLE_CH_INSTALL_DATA;
static uint8_t s_status_characteristic = BLE_CH_INSTALL_STATUS;
static uint8_t s_runtime_control_characteristic = BLE_CH_RUNTIME_CONTROL;
static uint8_t s_runtime_rx_characteristic = BLE_CH_RUNTIME_RX;
static uint8_t s_runtime_tx_characteristic = BLE_CH_RUNTIME_TX;
static uint8_t s_runtime_status_characteristic = BLE_CH_RUNTIME_STATUS;

/* UUID: f0771000-6f6c-6f74-6f79-70617373706f, characteristics end in 01/02/03. */
static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x00, 0x10, 0x77, 0xf0);
static const ble_uuid128_t s_control_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x01, 0x10, 0x77, 0xf0);
static const ble_uuid128_t s_data_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x02, 0x10, 0x77, 0xf0);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x03, 0x10, 0x77, 0xf0);

/* Runtime gateway: f0772000-... with auth/RX/TX/status ending in 01/02/03/04. */
static const ble_uuid128_t s_runtime_service_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x00, 0x20, 0x77, 0xf0);
static const ble_uuid128_t s_runtime_control_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x01, 0x20, 0x77, 0xf0);
static const ble_uuid128_t s_runtime_rx_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x02, 0x20, 0x77, 0xf0);
static const ble_uuid128_t s_runtime_tx_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x03, 0x20, 0x77, 0xf0);
static const ble_uuid128_t s_runtime_status_uuid = BLE_UUID128_INIT(
    0x6f, 0x70, 0x73, 0x73, 0x61, 0x70, 0x79, 0x6f,
    0x74, 0x6f, 0x6c, 0x6f, 0x04, 0x20, 0x77, 0xf0);

static int gap_event(struct ble_gap_event *event, void *context);
static int gatt_access(uint16_t connection, uint16_t attribute,
                       struct ble_gatt_access_ctxt *context, void *argument);

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_control_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_control_characteristic,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_data_characteristic,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_status_characteristic,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ble.status_handle,
            },
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_runtime_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_runtime_control_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_runtime_control_characteristic,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &s_runtime_rx_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_runtime_rx_characteristic,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_runtime_tx_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_runtime_tx_characteristic,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ble.runtime_tx_handle,
            },
            {
                .uuid = &s_runtime_status_uuid.u,
                .access_cb = gatt_access,
                .arg = &s_runtime_status_characteristic,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ble.runtime_status_handle,
            },
            {0},
        },
    },
    {0},
};

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool capture_authorized_session(uint16_t connection, uint32_t *generation)
{
    bool authorized;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    authorized = s_ble.running && s_ble.connection == connection &&
                 s_ble.sync_state == PLUGIN_BLE_SYNCED;
    if (authorized && generation) *generation = s_ble.session_generation;
    xSemaphoreGive(s_ble.mutex);
    return authorized;
}

static bool session_still_authorized(const ble_work_t *work)
{
    bool authorized;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    authorized = s_ble.running && s_ble.connection == work->connection &&
                 s_ble.session_generation == work->session_generation &&
                 s_ble.sync_state == PLUGIN_BLE_SYNCED;
    xSemaphoreGive(s_ble.mutex);
    return authorized;
}

static bool is_running(void)
{
    bool running;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    running = s_ble.running;
    xSemaphoreGive(s_ble.mutex);
    return running;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = value & 0xffU;
    data[1] = (value >> 8) & 0xffU;
    data[2] = (value >> 16) & 0xffU;
    data[3] = (value >> 24) & 0xffU;
}

static void notify_runtime_state(plugin_ble_sync_state_t state)
{
    plugin_ble_runtime_state_callback_t callback = NULL;
    void *context = NULL;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (s_ble.running && s_ble.mode == BLE_MODE_RUNTIME) {
        callback = s_ble.runtime_callbacks.state;
        context = s_ble.runtime_context;
    }
    xSemaphoreGive(s_ble.mutex);
    if (callback) callback(context, state);
}

static size_t make_status(uint8_t status[20])
{
    plugin_installer_snapshot_t snapshot;

    plugin_installer_snapshot(&snapshot);
    memset(status, 0, 20U);
    status[0] = BLE_STATUS_VERSION;
    status[1] = snapshot.state;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    status[2] = (uint8_t)s_ble.sync_state;
    xSemaphoreGive(s_ble.mutex);
    write_u32(status + 4U, (uint32_t)snapshot.error);
    write_u32(status + 8U, (uint32_t)snapshot.expected);
    write_u32(status + 12U, (uint32_t)snapshot.received);
    write_u32(status + 16U, snapshot.pending.plugin_version);
    return 20U;
}

static size_t make_runtime_status(uint8_t status[8])
{
    memset(status, 0, 8U);
    status[0] = BLE_RUNTIME_STATUS_VERSION;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    status[1] = (uint8_t)s_ble.sync_state;
    status[2] = s_ble.runtime_subscribed ? 1U : 0U;
    write_u32(status + 4U, s_ble.session_generation);
    xSemaphoreGive(s_ble.mutex);
    return 8U;
}

static void publish_status(void)
{
    bool publish;
    uint16_t status_handle;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    status_handle = s_ble.mode == BLE_MODE_RUNTIME ? s_ble.runtime_status_handle :
                                                    s_ble.status_handle;
    publish = s_ble.running && s_ble.connection != BLE_HS_CONN_HANDLE_NONE &&
              status_handle != 0U;
    xSemaphoreGive(s_ble.mutex);
    if (publish) ble_gatts_chr_updated(status_handle);
}

static void work_task(void *argument)
{
    ble_work_t work;
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_ble.queue, &work, portMAX_DELAY) != pdTRUE) continue;
        if (work.session_bound && !session_still_authorized(&work)) {
            continue;
        }
        switch (work.type) {
        case BLE_WORK_BEGIN:
            plugin_installer_begin(work.total_size);
            break;
        case BLE_WORK_DATA:
            plugin_installer_write(work.offset, work.data, work.size);
            break;
        case BLE_WORK_FINISH:
            plugin_installer_finish();
            break;
        case BLE_WORK_ABORT:
            plugin_installer_reject();
            plugin_installer_reset();
            break;
        case BLE_WORK_RESET:
            plugin_installer_reset();
            break;
        }
        publish_status();
    }
}

static int synchronize(uint16_t connection, const uint8_t *data, size_t size)
{
    plugin_ble_sync_state_t state;

    if (size != DEVICE_CODE_COMPACT_LENGTH) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (s_ble.connection != connection || !s_ble.running) {
        xSemaphoreGive(s_ble.mutex);
        return BLE_ATT_ERR_UNLIKELY;
    }
    state = device_identity_matches(data, size) ?
            PLUGIN_BLE_SYNCED : PLUGIN_BLE_CODE_MISMATCH;
    s_ble.sync_state = state;
    xSemaphoreGive(s_ble.mutex);
    publish_status();
    notify_runtime_state(state);
    return 0;
}

static int enqueue_control(uint16_t connection, const uint8_t *data, size_t size)
{
    ble_work_t work = {0};

    if (size == DEVICE_CODE_COMPACT_LENGTH + 1U && data[0] == BLE_CONTROL_SYNC) {
        return synchronize(connection, data + 1U, size - 1U);
    }
    if (!capture_authorized_session(connection, &work.session_generation)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    work.connection = connection;
    work.session_bound = true;
    if (size == 5U && data[0] == 1U) {
        work.type = BLE_WORK_BEGIN;
        work.total_size = read_u32(data + 1U);
    } else if (size == 1U && data[0] == 2U) {
        work.type = BLE_WORK_FINISH;
    } else if (size == 1U && data[0] == 3U) {
        work.type = BLE_WORK_ABORT;
    } else if (size == 1U && data[0] == 4U) {
        work.type = BLE_WORK_RESET;
    } else {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return xQueueSend(s_ble.queue, &work, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int enqueue_data(uint16_t connection, const uint8_t *data, size_t size)
{
    ble_work_t work = {
        .type = BLE_WORK_DATA,
        .connection = connection,
        .session_bound = true,
    };

    if (!capture_authorized_session(connection, &work.session_generation)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (size <= 4U || size - 4U > sizeof(work.data)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    work.offset = read_u32(data);
    work.size = (uint16_t)(size - 4U);
    memcpy(work.data, data + 4U, work.size);
    return xQueueSend(s_ble.queue, &work, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int receive_runtime_frame(uint16_t connection,
                                 const uint8_t *data, size_t size)
{
    plugin_ble_runtime_frame_callback_t callback = NULL;
    void *callback_context = NULL;

    if (size == 0U || size > BLE_ATTRIBUTE_MAX) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (s_ble.running && s_ble.mode == BLE_MODE_RUNTIME &&
        s_ble.connection == connection && s_ble.sync_state == PLUGIN_BLE_SYNCED) {
        callback = s_ble.runtime_callbacks.frame;
        callback_context = s_ble.runtime_context;
    }
    xSemaphoreGive(s_ble.mutex);
    if (!callback) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    return callback(callback_context, data, size) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_access(uint16_t connection, uint16_t attribute,
                       struct ble_gatt_access_ctxt *context, void *argument)
{
    uint8_t buffer[BLE_ATTRIBUTE_MAX];
    uint16_t length = 0;
    uint8_t characteristic = *(const uint8_t *)argument;
    ble_mode_t mode;
    (void)attribute;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    mode = s_ble.mode;
    xSemaphoreGive(s_ble.mutex);

    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        characteristic == BLE_CH_INSTALL_STATUS && mode == BLE_MODE_INSTALLER) {
        length = (uint16_t)make_status(buffer);
        return os_mbuf_append(context->om, buffer, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        characteristic == BLE_CH_RUNTIME_STATUS && mode == BLE_MODE_RUNTIME) {
        length = (uint16_t)make_runtime_status(buffer);
        return os_mbuf_append(context->om, buffer, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    bool installer_characteristic = mode == BLE_MODE_INSTALLER &&
        (characteristic == BLE_CH_INSTALL_CONTROL ||
         characteristic == BLE_CH_INSTALL_DATA);
    bool runtime_characteristic = mode == BLE_MODE_RUNTIME &&
        (characteristic == BLE_CH_RUNTIME_CONTROL ||
         characteristic == BLE_CH_RUNTIME_RX);
    if (!installer_characteristic && !runtime_characteristic) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (ble_hs_mbuf_to_flat(context->om, buffer, sizeof(buffer), &length) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (characteristic == BLE_CH_INSTALL_CONTROL) {
        return enqueue_control(connection, buffer, length);
    }
    if (characteristic == BLE_CH_INSTALL_DATA) {
        return enqueue_data(connection, buffer, length);
    }
    if (characteristic == BLE_CH_RUNTIME_CONTROL) {
        if (length != DEVICE_CODE_COMPACT_LENGTH + 1U ||
            buffer[0] != BLE_CONTROL_SYNC) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        return synchronize(connection, buffer + 1U, length - 1U);
    }
    return receive_runtime_frame(connection, buffer, length);
}

static int start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields response = {0};
    struct ble_gap_adv_params parameters = {0};
    const ble_uuid128_t *service_uuid;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    service_uuid = s_ble.mode == BLE_MODE_RUNTIME ? &s_runtime_service_uuid :
                                                   &s_service_uuid;
    xSemaphoreGive(s_ble.mutex);

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) return result;
    response.name = (uint8_t *)s_device_name;
    response.name_len = strlen(s_device_name);
    response.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&response);
    if (result != 0) return result;

    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_ble.own_address_type, NULL, BLE_HS_FOREVER,
                             &parameters, gap_event, NULL);
}

static void on_sync(void)
{
    if (!is_running()) return;
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) result = ble_hs_id_infer_auto(0, &s_ble.own_address_type);
    if (result == 0) {
        result = start_advertising();
    }
    if (result != 0) ESP_LOGE(TAG, "BLE sync/advertise failed: %d", result);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static int gap_event(struct ble_gap_event *event, void *context)
{
    (void)context;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            bool runtime;
            xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
            s_ble.connection = event->connect.conn_handle;
            ++s_ble.session_generation;
            s_ble.sync_state = PLUGIN_BLE_CONNECTED;
            s_ble.runtime_subscribed = false;
            runtime = s_ble.mode == BLE_MODE_RUNTIME;
            xSemaphoreGive(s_ble.mutex);
            ESP_LOGI(TAG, "client connected");
            publish_status();
            if (runtime) notify_runtime_state(PLUGIN_BLE_CONNECTED);
        } else if (is_running()) {
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT: {
        ble_work_t abort_work = { .type = BLE_WORK_ABORT };
        bool runtime;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        runtime = s_ble.mode == BLE_MODE_RUNTIME;
        s_ble.connection = BLE_HS_CONN_HANDLE_NONE;
        ++s_ble.session_generation;
        s_ble.sync_state = PLUGIN_BLE_DISCONNECTED;
        s_ble.runtime_subscribed = false;
        xSemaphoreGive(s_ble.mutex);
        if (runtime) notify_runtime_state(PLUGIN_BLE_DISCONNECTED);
        else xQueueSendToFront(s_ble.queue, &abort_work, 0);
        if (is_running()) start_advertising();
        return 0;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (is_running()) start_advertising();
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        bool runtime_status = false;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        if (s_ble.mode == BLE_MODE_RUNTIME &&
            event->subscribe.attr_handle == s_ble.runtime_tx_handle) {
            s_ble.runtime_subscribed = event->subscribe.cur_notify != 0;
            runtime_status = true;
        }
        xSemaphoreGive(s_ble.mutex);
        if (runtime_status) publish_status();
        return 0;
    }
    default:
        return 0;
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t worker_init(void)
{
    for (;;) {
        portENTER_CRITICAL(&s_worker_init_lock);
        if (s_ble.worker_ready) {
            portEXIT_CRITICAL(&s_worker_init_lock);
            return ESP_OK;
        }
        if (!s_worker_initializing) {
            s_worker_initializing = true;
            portEXIT_CRITICAL(&s_worker_init_lock);
            break;
        }
        portEXIT_CRITICAL(&s_worker_init_lock);
        vTaskDelay(1);
    }

    s_ble.mutex = xSemaphoreCreateMutex();
    s_ble.lifecycle_mutex = xSemaphoreCreateMutex();
    s_ble.queue = xQueueCreate(BLE_WORK_QUEUE_DEPTH, sizeof(ble_work_t));
    if (!s_ble.mutex || !s_ble.lifecycle_mutex || !s_ble.queue) {
        if (s_ble.queue) vQueueDelete(s_ble.queue);
        if (s_ble.mutex) vSemaphoreDelete(s_ble.mutex);
        if (s_ble.lifecycle_mutex) vSemaphoreDelete(s_ble.lifecycle_mutex);
        s_ble.queue = NULL;
        s_ble.mutex = NULL;
        s_ble.lifecycle_mutex = NULL;
        portENTER_CRITICAL(&s_worker_init_lock);
        s_worker_initializing = false;
        portEXIT_CRITICAL(&s_worker_init_lock);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(work_task, "plugin_ble_rx", 3584, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_ble.queue);
        vSemaphoreDelete(s_ble.mutex);
        vSemaphoreDelete(s_ble.lifecycle_mutex);
        s_ble.queue = NULL;
        s_ble.mutex = NULL;
        s_ble.lifecycle_mutex = NULL;
        portENTER_CRITICAL(&s_worker_init_lock);
        s_worker_initializing = false;
        portEXIT_CRITICAL(&s_worker_init_lock);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_worker_init_lock);
    s_ble.worker_ready = true;
    s_worker_initializing = false;
    portEXIT_CRITICAL(&s_worker_init_lock);
    return ESP_OK;
}

/* lifecycle_mutex must be held for the entire stop/deinit transaction. */
static esp_err_t stop_mode_locked(ble_mode_t expected_mode)
{
    if (!s_ble.stack_ready) return ESP_OK;

    uint16_t connection;
    bool was_running;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (expected_mode != BLE_MODE_NONE && s_ble.mode != expected_mode) {
        xSemaphoreGive(s_ble.mutex);
        return ESP_OK;
    }
    was_running = s_ble.running;
    s_ble.running = false;
    connection = s_ble.connection;
    xSemaphoreGive(s_ble.mutex);

    if (connection != BLE_HS_CONN_HANDLE_NONE) {
        int disconnect_result = ble_gap_terminate(
            connection, BLE_ERR_REM_USER_CONN_TERM);
        if (disconnect_result != 0 && disconnect_result != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "disconnect before stop failed: %d", disconnect_result);
        }
        for (size_t attempt = 0; attempt < 50U; ++attempt) {
            xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
            bool disconnected = s_ble.connection == BLE_HS_CONN_HANDLE_NONE;
            xSemaphoreGive(s_ble.mutex);
            if (disconnected) break;
            vTaskDelay(pdMS_TO_TICKS(10U));
        }
    }
    if (nimble_port_stop() != 0) {
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        s_ble.running = was_running;
        xSemaphoreGive(s_ble.mutex);
        return ESP_FAIL;
    }
    esp_err_t result = nimble_port_deinit();
    if (result != ESP_OK) return result;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    s_ble.stack_ready = false;
    s_ble.status_handle = 0;
    s_ble.runtime_tx_handle = 0;
    s_ble.runtime_status_handle = 0;
    s_ble.connection = BLE_HS_CONN_HANDLE_NONE;
    ++s_ble.session_generation;
    s_ble.sync_state = PLUGIN_BLE_DISCONNECTED;
    s_ble.mode = BLE_MODE_NONE;
    memset(&s_ble.runtime_callbacks, 0, sizeof(s_ble.runtime_callbacks));
    s_ble.runtime_context = NULL;
    s_ble.runtime_subscribed = false;
    xSemaphoreGive(s_ble.mutex);
    ESP_LOGI(TAG, "BLE gateway stopped");
    return ESP_OK;
}

static esp_err_t start_mode(ble_mode_t mode,
                            const plugin_ble_runtime_callbacks_t *callbacks,
                            void *context)
{
    esp_err_t result;
    int rc;

    if (mode != BLE_MODE_INSTALLER && mode != BLE_MODE_RUNTIME) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == BLE_MODE_RUNTIME && (!callbacks || !callbacks->frame)) {
        return ESP_ERR_INVALID_ARG;
    }
    result = worker_init();
    if (result != ESP_OK) return result;

    xSemaphoreTake(s_ble.lifecycle_mutex, portMAX_DELAY);
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    bool stack_ready = s_ble.stack_ready;
    bool running = s_ble.running;
    ble_mode_t running_mode = s_ble.mode;
    xSemaphoreGive(s_ble.mutex);
    if (stack_ready) {
        if (running && running_mode == mode) {
            result = ESP_OK;
            goto finish;
        }
        /* Runtime owns priority: entering a foreground plugin atomically
         * replaces an installer session without exposing an interleaving gap. */
        if (mode == BLE_MODE_RUNTIME && running_mode == BLE_MODE_INSTALLER) {
            result = stop_mode_locked(BLE_MODE_INSTALLER);
            if (result != ESP_OK) goto finish;
        } else {
            result = ESP_ERR_INVALID_STATE;
            goto finish;
        }
    }
    const char *device_code = device_identity_code_compact();
    if (strlen(device_code) != DEVICE_CODE_COMPACT_LENGTH) {
        result = ESP_ERR_INVALID_STATE;
        goto finish;
    }
    snprintf(s_device_name, sizeof(s_device_name), "Passport-%s",
             device_code + DEVICE_CODE_COMPACT_LENGTH - 4U);

    result = nimble_port_init();
    if (result != ESP_OK) goto finish;
    s_ble.stack_ready = true;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(s_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_services);
    if (rc == 0) rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc == 0) rc = ble_att_set_preferred_mtu(256U);
    if (rc != 0) {
        nimble_port_deinit();
        s_ble.stack_ready = false;
        result = ESP_FAIL;
        goto finish;
    }
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    s_ble.connection = BLE_HS_CONN_HANDLE_NONE;
    ++s_ble.session_generation;
    s_ble.sync_state = PLUGIN_BLE_DISCONNECTED;
    s_ble.mode = mode;
    s_ble.runtime_callbacks = callbacks ? *callbacks :
        (plugin_ble_runtime_callbacks_t) {0};
    s_ble.runtime_context = context;
    s_ble.runtime_subscribed = false;
    s_ble.running = true;
    xSemaphoreGive(s_ble.mutex);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "unpaired %s advertising as %s",
             mode == BLE_MODE_RUNTIME ? "runtime gateway" : "installer",
             s_device_name);
    result = ESP_OK;

finish:
    xSemaphoreGive(s_ble.lifecycle_mutex);
    return result;
}

esp_err_t plugin_ble_start(void)
{
    return start_mode(BLE_MODE_INSTALLER, NULL, NULL);
}

esp_err_t plugin_ble_start_runtime(const plugin_ble_runtime_callbacks_t *callbacks,
                                   void *context)
{
    return start_mode(BLE_MODE_RUNTIME, callbacks, context);
}

static void stop_mode(ble_mode_t mode)
{
    if (!s_ble.worker_ready) return;
    xSemaphoreTake(s_ble.lifecycle_mutex, portMAX_DELAY);
    esp_err_t result = stop_mode_locked(mode);
    xSemaphoreGive(s_ble.lifecycle_mutex);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "failed to stop BLE gateway: %s",
                 esp_err_to_name(result));
    }
}

void plugin_ble_stop_installer(void)
{
    stop_mode(BLE_MODE_INSTALLER);
}

void plugin_ble_stop_runtime(void)
{
    stop_mode(BLE_MODE_RUNTIME);
}

bool plugin_ble_running(void)
{
    return s_ble.worker_ready && is_running();
}

static bool running_in_mode(ble_mode_t mode)
{
    bool running;
    if (!s_ble.worker_ready) return false;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    running = s_ble.running && s_ble.mode == mode;
    xSemaphoreGive(s_ble.mutex);
    return running;
}

bool plugin_ble_installer_running(void)
{
    return running_in_mode(BLE_MODE_INSTALLER);
}

bool plugin_ble_runtime_running(void)
{
    return running_in_mode(BLE_MODE_RUNTIME);
}

plugin_ble_sync_state_t plugin_ble_sync_state(void)
{
    plugin_ble_sync_state_t state = PLUGIN_BLE_DISCONNECTED;
    if (!s_ble.worker_ready) return state;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    state = s_ble.sync_state;
    xSemaphoreGive(s_ble.mutex);
    return state;
}

bool plugin_ble_runtime_send(const uint8_t *data, size_t size)
{
    uint16_t connection;
    uint16_t handle;
    bool ready;

    if (!data || size == 0U || size > BLE_ATTRIBUTE_MAX || !s_ble.worker_ready) {
        return false;
    }
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    ready = s_ble.running && s_ble.mode == BLE_MODE_RUNTIME &&
            s_ble.sync_state == PLUGIN_BLE_SYNCED && s_ble.runtime_subscribed &&
            s_ble.connection != BLE_HS_CONN_HANDLE_NONE &&
            s_ble.runtime_tx_handle != 0U;
    connection = s_ble.connection;
    handle = s_ble.runtime_tx_handle;
    xSemaphoreGive(s_ble.mutex);
    if (!ready) return false;

    struct os_mbuf *packet = ble_hs_mbuf_from_flat(data, (uint16_t)size);
    if (!packet) return false;
    return ble_gatts_notify_custom(connection, handle, packet) == 0;
}
