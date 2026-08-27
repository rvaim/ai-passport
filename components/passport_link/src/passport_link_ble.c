#include "passport_link.h"

#include "passport_crc32.h"
#include "passport_identity.h"
#include "passport_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "passport_link";

/* UUID bytes are private to Passport Link v1. Keep them stable once clients ship. */
static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x4c,0x49,0x4e,0x4b,0x00,0x00,0x01);
static const ble_uuid128_t RX_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x52,0x58,0x00,0x00,0x00,0x00,0x01);
static const ble_uuid128_t TX_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x54,0x58,0x00,0x00,0x00,0x00,0x01);
static const ble_uuid128_t CODE_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x43,0x4f,0x44,0x45,0x00,0x00,0x01);
static const ble_uuid128_t PKG_CTRL_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x50,0x4b,0x47,0x43,0x00,0x00,0x01);
static const ble_uuid128_t PKG_DATA_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x50,0x4b,0x47,0x44,0x00,0x00,0x01);
static const ble_uuid128_t PKG_STATUS_UUID = BLE_UUID128_INIT(0x31,0x50,0x41,0x53,0x53,0x50,0x4f,0x52,0x54,0x50,0x4b,0x47,0x53,0x00,0x00,0x01);

#define LINK_CONN_NONE 0xFFFF
#define PKG_CHUNK_MAX 244
#define PKG_QUEUE_DEPTH 8
#define PKG_WORKER_STACK_BYTES 6144

typedef enum {
    WORK_PKG_BEGIN = 1,
    WORK_PKG_DATA,
    WORK_PKG_END,
    WORK_PKG_ABORT,
} work_type_t;

typedef struct {
    work_type_t type;
    uint16_t len;
    uint8_t bytes[PKG_CHUNK_MAX];
} work_item_t;

typedef struct __attribute__((packed)) {
    uint8_t op;            /* 1 = begin, 2 = end */
    uint32_t total_size;   /* little-endian host value; ESP32-C3 is little-endian */
    uint32_t crc32;
    uint64_t target_id;
} pkg_control_t;

static uint8_t s_addr_type;
static uint16_t s_conn_handle = LINK_CONN_NONE;
static uint16_t s_tx_handle;
static uint16_t s_pkg_status_handle;
static bool s_tx_subscribed;
static bool s_pkg_status_subscribed;
static bool s_started;
static QueueHandle_t s_queue;
static passport_link_rx_cb_t s_rx_cb;
static void *s_rx_user;
static passport_link_install_cb_t s_install_cb;
static void *s_install_user;
static uint32_t s_tx_sequence;

/* Package writer state is owned exclusively by the worker task. */
static FILE *s_pkg_file;
static uint32_t s_pkg_expected_size;
static uint32_t s_pkg_expected_crc;
static uint32_t s_pkg_received;
static uint32_t s_pkg_crc;

static int gap_event(struct ble_gap_event *event, void *arg);

static int append_mbuf(struct os_mbuf *om, const void *data, size_t len)
{
    return os_mbuf_append(om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void notify_text(uint16_t attr_handle, bool subscribed, const char *text)
{
    if (!subscribed || s_conn_handle == LINK_CONN_NONE || !text) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(text, strlen(text));
    if (om) ble_gatts_notify_custom(s_conn_handle, attr_handle, om);
}

static void package_reset(void)
{
    if (s_pkg_file) fclose(s_pkg_file);
    s_pkg_file = NULL;
    s_pkg_expected_size = 0;
    s_pkg_expected_crc = 0;
    s_pkg_received = 0;
    s_pkg_crc = 0;
}

static void package_finish(void)
{
    esp_err_t result = ESP_OK;
    passport_package_result_t installed = {0};
    if (!s_pkg_file) result = ESP_ERR_INVALID_STATE;
    if (s_pkg_file && fclose(s_pkg_file) != 0) result = ESP_FAIL;
    s_pkg_file = NULL;
    if (result == ESP_OK && (s_pkg_received != s_pkg_expected_size || s_pkg_crc != s_pkg_expected_crc)) {
        result = ESP_ERR_INVALID_CRC;
    }
    if (result == ESP_OK) result = passport_package_install(PASSPORT_INCOMING_PACKAGE, &installed);
    unlink(PASSPORT_INCOMING_PACKAGE);

    if (result == ESP_OK) notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "安装成功");
    else notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "安装失败");
    if (s_install_cb) s_install_cb(result, result == ESP_OK ? &installed : NULL, s_install_user);
    package_reset();
    ESP_LOGI(TAG, "pap_install 最小剩余栈: %u B",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void package_worker(void *arg)
{
    (void)arg;
    work_item_t item;
    while (xQueueReceive(s_queue, &item, portMAX_DELAY) == pdTRUE) {
        if (item.type == WORK_PKG_BEGIN) {
            if (item.len != sizeof(pkg_control_t)) continue;
            pkg_control_t ctrl;
            memcpy(&ctrl, item.bytes, sizeof(ctrl));
            package_reset();
            unlink(PASSPORT_INCOMING_PACKAGE);
            if (ctrl.target_id != passport_identity_id() || ctrl.total_size == 0 ||
                ctrl.total_size > PASSPORT_PACKAGE_TRANSFER_MAX_BYTES) {
                notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "设备码不匹配");
                continue;
            }
            s_pkg_file = fopen(PASSPORT_INCOMING_PACKAGE, "wb");
            if (!s_pkg_file) {
                notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "无法写入存储");
                continue;
            }
            s_pkg_expected_size = ctrl.total_size;
            s_pkg_expected_crc = ctrl.crc32;
            notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "开始接收");
        } else if (item.type == WORK_PKG_DATA) {
            if (!s_pkg_file || item.len == 0 || s_pkg_received + item.len > s_pkg_expected_size) continue;
            if (fwrite(item.bytes, 1, item.len, s_pkg_file) != item.len) {
                notify_text(s_pkg_status_handle, s_pkg_status_subscribed, "写入失败");
                package_reset();
                unlink(PASSPORT_INCOMING_PACKAGE);
                continue;
            }
            s_pkg_crc = passport_crc32_update(s_pkg_crc, item.bytes, item.len);
            s_pkg_received += item.len;
        } else if (item.type == WORK_PKG_END) {
            package_finish();
        } else if (item.type == WORK_PKG_ABORT) {
            package_reset();
            unlink(PASSPORT_INCOMING_PACKAGE);
        }
    }
}

static bool queue_work(work_type_t type, const void *data, size_t len)
{
    if (!s_queue || len > PKG_CHUNK_MAX) return false;
    work_item_t item = {.type = type, .len = (uint16_t)len};
    if (len) memcpy(item.bytes, data, len);
    return xQueueSend(s_queue, &item, 0) == pdTRUE;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    uintptr_t role = (uintptr_t)arg;
    if (role == 1) { /* public device code read */
        const char *code = passport_identity_code();
        return append_mbuf(ctxt->om, code, strlen(code));
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > PKG_CHUNK_MAX) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t buffer[PKG_CHUNK_MAX];
    uint16_t copied = 0;
    if (len && ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &copied) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (role == 2) { /* app/link RX frame */
        passport_link_frame_t frame;
        if (passport_link_frame_decode(buffer, copied, &frame) != ESP_OK) return BLE_ATT_ERR_UNLIKELY;
        if (frame.target_id != passport_identity_id()) return 0; /* wrong device: silently drop */
        if (s_rx_cb) s_rx_cb(&frame, s_rx_user);
        return 0;
    }
    if (role == 3) { /* package control */
        if (copied < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (buffer[0] == 1) {
            if (copied != sizeof(pkg_control_t) || !queue_work(WORK_PKG_BEGIN, buffer, copied)) return BLE_ATT_ERR_INSUFFICIENT_RES;
        } else if (buffer[0] == 2) {
            if (!queue_work(WORK_PKG_END, NULL, 0)) return BLE_ATT_ERR_INSUFFICIENT_RES;
        } else return BLE_ATT_ERR_UNLIKELY;
        return 0;
    }
    if (role == 4) { /* package byte stream */
        if (!queue_work(WORK_PKG_DATA, buffer, copied)) return BLE_ATT_ERR_INSUFFICIENT_RES;
        return 0;
    }
    if (role == 5) { /* package status read */
        static const char ready[] = "就绪";
        return append_mbuf(ctxt->om, ready, sizeof(ready) - 1);
    }
    (void)attr_handle;
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def GATT_CHRS[] = {
    {.uuid = &RX_UUID.u, .access_cb = gatt_access, .arg = (void *)2,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = &TX_UUID.u, .access_cb = gatt_access, .arg = (void *)0,
     .val_handle = &s_tx_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
    {.uuid = &CODE_UUID.u, .access_cb = gatt_access, .arg = (void *)1,
     .flags = BLE_GATT_CHR_F_READ},
    {.uuid = &PKG_CTRL_UUID.u, .access_cb = gatt_access, .arg = (void *)3,
     .flags = BLE_GATT_CHR_F_WRITE},
    {.uuid = &PKG_DATA_UUID.u, .access_cb = gatt_access, .arg = (void *)4,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = &PKG_STATUS_UUID.u, .access_cb = gatt_access, .arg = (void *)5,
     .val_handle = &s_pkg_status_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
    {0},
};

static const struct ble_gatt_svc_def GATT_SERVICES[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &SVC_UUID.u, .characteristics = GATT_CHRS},
    {0},
};

static int advertise(void)
{
    char name[30];
    snprintf(name, sizeof(name), "Passport-%s", passport_identity_code());
    ble_svc_gap_device_name_set(name);
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    /* A BLE 4.x 31-byte advertisement cannot hold flags, the 128-bit service
     * UUID, and the complete device name together. Keep discovery data in the
     * advertisement for Web Bluetooth filters and move the name to scan response. */
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)name;
    response.name_len = strlen(name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
    s_conn_handle = LINK_CONN_NONE;
    s_tx_subscribed = false;
    s_pkg_status_subscribed = false;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) rc = advertise();
    if (rc != 0) ESP_LOGE(TAG, "BLE 广播启动失败: %d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE 已连接");
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = LINK_CONN_NONE;
        s_tx_subscribed = false;
        s_pkg_status_subscribed = false;
        queue_work(WORK_PKG_ABORT, NULL, 0);
        advertise();
        ESP_LOGI(TAG, "BLE 已断开");
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) s_tx_subscribed = event->subscribe.cur_notify != 0;
        if (event->subscribe.attr_handle == s_pkg_status_handle) s_pkg_status_subscribed = event->subscribe.cur_notify != 0;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t passport_link_init(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(GATT_SERVICES);
    if (rc == 0) rc = ble_gatts_add_svcs(GATT_SERVICES);
    if (rc != 0) {
        nimble_port_deinit();
        return ESP_FAIL;
    }
    s_queue = xQueueCreate(PKG_QUEUE_DEPTH, sizeof(work_item_t));
    if (!s_queue) {
        nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    /* FATFS transaction handling and cJSON parsing exceeded the original 4 KiB
     * stack in measured hardware installs. Six KiB leaves bounded headroom;
     * package_finish logs the observed minimum reserve after each transaction. */
    if (xTaskCreate(package_worker, "pap_install", PKG_WORKER_STACK_BYTES,
                    NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    s_started = true;
    ESP_LOGI(TAG, "Passport Link 已启动，无系统配对，设备码=%s", passport_identity_code());
    return ESP_OK;
}

void passport_link_set_rx_callback(passport_link_rx_cb_t cb, void *user)
{
    s_rx_cb = cb;
    s_rx_user = user;
}

void passport_link_set_install_callback(passport_link_install_cb_t cb, void *user)
{
    s_install_cb = cb;
    s_install_user = user;
}

esp_err_t passport_link_send(uint64_t target_id, const char *service_name, uint8_t type,
                             const void *payload, size_t payload_len)
{
    if (s_conn_handle == LINK_CONN_NONE || !s_tx_subscribed) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t frame[PASSPORT_LINK_HEADER_SIZE + PASSPORT_LINK_MAX_PAYLOAD];
    size_t frame_len = 0;
    esp_err_t err = passport_link_frame_encode(type, passport_identity_id(), target_id,
                                                passport_link_service_id(service_name), ++s_tx_sequence,
                                                payload, payload_len, frame, sizeof(frame), &frame_len);
    if (err != ESP_OK) return err;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, frame_len);
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
