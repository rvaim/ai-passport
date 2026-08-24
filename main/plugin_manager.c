#include "plugin_manager.h"

#include "device_identity.h"
#include "plugin_ble.h"
#include "plugin_installer.h"
#include "plugin_manager_model.h"
#include "plugin_store.h"
#include "ui_pixel.h"
#include "ui_theme.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MANAGER_VISIBLE_ROWS 4U
#define NOTICE_DURATION_US 2000000LL

typedef enum {
    RADIO_START,
    RADIO_STOP,
} radio_request_t;

typedef struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    radio_request_t requested;
    esp_err_t error;
    bool switching;
    bool initialized;
} radio_state_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_count_label;
static lv_obj_t *s_rows[MANAGER_VISIBLE_ROWS];
static lv_obj_t *s_row_labels[MANAGER_VISIBLE_ROWS];
static lv_obj_t *s_empty_panel;
static lv_obj_t *s_action_bar;
static char s_action_navigation[25];
static char s_action_ok[25];
static char s_action_back[25];
static ui_pixel_dialog_t s_install_dialog;
static ui_pixel_dialog_t s_uninstall_dialog;
static lv_timer_t *s_refresh_timer;
static plugin_record_t s_records[PLUGIN_STORE_MAX_ACTIVE];
static size_t s_record_count;
static plugin_manager_model_t s_model;
static char s_uninstall_id[PLUGIN_ID_SIZE];
static char s_notice[96];
static int64_t s_notice_until;
static plugin_install_state_t s_last_install_state;
static radio_state_t s_radio;
static const char *TAG = "plugin_manager";

static const char *permission_text(uint32_t permissions)
{
    if (permissions == 0U) return "无";
    if (permissions == PLUGIN_PERMISSION_STORAGE) return "存储";
    if (permissions == PLUGIN_PERMISSION_AUDIO) return "音频";
    if (permissions == (PLUGIN_PERMISSION_STORAGE | PLUGIN_PERMISSION_AUDIO)) {
        return "存储、音频";
    }
    if (permissions == PLUGIN_PERMISSION_SETTINGS) return "系统设置";
    if (permissions & PLUGIN_PERMISSION_SETTINGS) return "含系统设置";
    if (permissions & PLUGIN_PERMISSION_MICROPHONE) return "含麦克风";
    if (permissions & PLUGIN_PERMISSION_NEARBY) return "含近场通信";
    return "未知";
}

static const char *kind_text(uint8_t kind)
{
    return kind == PLUGIN_KIND_THEME ? "主题" : "应用";
}

static void set_actions(const char *navigation, const char *ok, const char *back)
{
    if (strcmp(s_action_navigation, navigation) == 0 &&
        strcmp(s_action_ok, ok) == 0 && strcmp(s_action_back, back) == 0) {
        return;
    }
    snprintf(s_action_navigation, sizeof(s_action_navigation), "%s", navigation);
    snprintf(s_action_ok, sizeof(s_action_ok), "%s", ok);
    snprintf(s_action_back, sizeof(s_action_back), "%s", back);
    if (s_action_bar) lv_obj_delete(s_action_bar);
    s_action_bar = ui_pixel_action_bar(s_screen, navigation, ok, back);
}

static void sort_records(void)
{
    for (size_t index = 1U; index < s_record_count; ++index) {
        plugin_record_t value = s_records[index];
        size_t cursor = index;
        while (cursor > 0U &&
               strcmp(s_records[cursor - 1U].manifest.name, value.manifest.name) > 0) {
            s_records[cursor] = s_records[cursor - 1U];
            --cursor;
        }
        s_records[cursor] = value;
    }
}

static void load_records(void)
{
    s_record_count = plugin_store_list(s_records, PLUGIN_STORE_MAX_ACTIVE);
    if (s_record_count > PLUGIN_STORE_MAX_ACTIVE) {
        s_record_count = PLUGIN_STORE_MAX_ACTIVE;
    }
    sort_records();
    plugin_manager_model_set_count(&s_model, s_record_count);
}

static void refresh_list(void)
{
    lv_label_set_text_fmt(s_count_label, "%u 个", (unsigned)s_record_count);
    if (s_record_count == 0U) {
        lv_obj_remove_flag(s_empty_panel, LV_OBJ_FLAG_HIDDEN);
        for (size_t row = 0; row < MANAGER_VISIBLE_ROWS; ++row) {
            lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_add_flag(s_empty_panel, LV_OBJ_FLAG_HIDDEN);
    size_t window = plugin_manager_model_window(&s_model, MANAGER_VISIBLE_ROWS);
    for (size_t row = 0; row < MANAGER_VISIBLE_ROWS; ++row) {
        size_t index = window + row;
        if (index >= s_record_count) {
            lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const plugin_record_t *record = &s_records[index];
        lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_row_labels[row], "%s  %s v%lu",
                              record->manifest.name, kind_text(record->manifest.kind),
                              (unsigned long)record->manifest.plugin_version);
        ui_pixel_set_selected(s_rows[row], index == s_model.selected, true);
    }
}

static void build_manager_screen(void)
{
    s_screen = ui_pixel_screen_create("插件");
    lv_obj_t *code_panel = ui_pixel_panel_create(s_screen, 12, 38, 216, 47, UI_YELLOW);
    lv_obj_t *code = ui_pixel_label(
        code_panel, device_identity_code(), &lv_font_montserrat_20, UI_INK);
    lv_obj_center(code);

    s_status = ui_pixel_label(s_screen, "", UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(s_status, 8, 92);
    lv_obj_set_width(s_status, 224);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);

    lv_obj_t *heading = ui_pixel_label(s_screen, "已安装插件", UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(heading, 10, 112);
    s_count_label = ui_pixel_label(s_screen, "", UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(s_count_label, 166, 112);
    lv_obj_set_width(s_count_label, 64);
    lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_RIGHT, 0);

    for (size_t row = 0; row < MANAGER_VISIBLE_ROWS; ++row) {
        s_rows[row] = ui_pixel_panel_create(
            s_screen, 10, 132 + (int)row * 39, 220, 33, UI_PAPER);
        s_row_labels[row] = ui_pixel_label(s_rows[row], "", UI_FONT_BODY, UI_INK);
        lv_obj_set_width(s_row_labels[row], 190);
        lv_label_set_long_mode(s_row_labels[row], LV_LABEL_LONG_DOT);
        lv_obj_align(s_row_labels[row], LV_ALIGN_LEFT_MID, 3, 0);
    }

    s_empty_panel = ui_pixel_empty_state(
        s_screen, 10, 132, 220, 67, "暂无插件\n等待网页安装");

    s_action_bar = NULL;
    s_action_navigation[0] = '\0';
    s_action_ok[0] = '\0';
    s_action_back[0] = '\0';
    memset(&s_install_dialog, 0, sizeof(s_install_dialog));
    memset(&s_uninstall_dialog, 0, sizeof(s_uninstall_dialog));
    ui_pixel_dialog_create(&s_install_dialog, s_screen);
    ui_pixel_dialog_create(&s_uninstall_dialog, s_screen);
    refresh_list();
    lv_screen_load(s_screen);
}

static void radio_snapshot(esp_err_t *error, bool *switching)
{
    xSemaphoreTake(s_radio.mutex, portMAX_DELAY);
    *error = s_radio.error;
    *switching = s_radio.switching;
    xSemaphoreGive(s_radio.mutex);
}

static void show_notice(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(s_notice, sizeof(s_notice), format, arguments);
    va_end(arguments);
    s_notice_until = esp_timer_get_time() + NOTICE_DURATION_US;
}

static void close_uninstall_dialog(void)
{
    plugin_manager_model_cancel_remove(&s_model);
    ui_pixel_dialog_hide(&s_uninstall_dialog);
}

static void refresh_uninstall_options(void)
{
    ui_pixel_dialog_set_selected(&s_uninstall_dialog, s_model.allow_remove);
}

static void open_uninstall_dialog(void)
{
    if (!plugin_manager_model_begin_remove(&s_model)) return;
    const plugin_record_t *record = &s_records[s_model.selected];
    snprintf(s_uninstall_id, sizeof(s_uninstall_id), "%s", record->manifest.id);
    ESP_LOGI(TAG, "uninstall prompt opened for %s", s_uninstall_id);
    ui_pixel_dialog_show_confirm(&s_uninstall_dialog, "卸载插件?",
                                 record->manifest.name, "否，保留", "是，卸载",
                                 false);
    refresh_uninstall_options();
    set_actions("选择", "确认", "取消");
}

static void uninstall_selected(void)
{
    char name[PLUGIN_NAME_SIZE];
    bool active_theme = ui_theme_is_active(s_uninstall_id);
    snprintf(name, sizeof(name), "%s", s_records[s_model.selected].manifest.name);
    close_uninstall_dialog();
    lv_label_set_text_fmt(s_status, "正在卸载 %s", name);
    set_actions("", "", "");
    lv_refr_now(NULL);

    ESP_LOGI(TAG, "uninstall confirmed for %s", s_uninstall_id);
    esp_err_t result = plugin_store_remove(s_uninstall_id);
    if (result == ESP_OK) {
        ui_theme_refresh();
        load_records();
        if (active_theme) {
            lv_obj_delete(s_screen);
            build_manager_screen();
        } else {
            refresh_list();
        }
        show_notice("已卸载 %s", name);
    } else {
        ESP_LOGE(TAG, "uninstall %s failed: %s", s_uninstall_id,
                 esp_err_to_name(result));
        show_notice("卸载失败：%s", esp_err_to_name(result));
    }
}

static void refresh_install_dialog(const plugin_installer_snapshot_t *snapshot)
{
    bool visible = snapshot->state == PLUGIN_INSTALL_WAITING_APPROVAL;
    if (!visible) {
        ui_pixel_dialog_hide(&s_install_dialog);
        return;
    }
    char details[160];
    snprintf(details, sizeof(details), "%s  v%lu\n类型：%s  作者：%s\n权限：%s",
             snapshot->pending.name,
             (unsigned long)snapshot->pending.plugin_version,
             kind_text(snapshot->pending.kind), snapshot->pending.author,
             permission_text(snapshot->pending.permissions));
    ui_pixel_dialog_show_message(&s_install_dialog, "确认安装", details);
}

static void refresh_ui(lv_timer_t *timer)
{
    plugin_installer_snapshot_t snapshot;
    esp_err_t radio_error;
    bool switching;
    (void)timer;

    plugin_installer_snapshot(&snapshot);
    radio_snapshot(&radio_error, &switching);
    if (snapshot.state != PLUGIN_INSTALL_IDLE && s_model.confirmation_open) {
        close_uninstall_dialog();
    }
    if (snapshot.state == PLUGIN_INSTALL_COMPLETE &&
        s_last_install_state != PLUGIN_INSTALL_COMPLETE) {
        ui_theme_refresh();
        load_records();
        refresh_list();
    }
    s_last_install_state = snapshot.state;
    refresh_install_dialog(&snapshot);

    if (s_model.confirmation_open) return;
    if (s_notice_until > esp_timer_get_time()) {
        lv_label_set_text(s_status, s_notice);
        set_actions(s_record_count > 0U ? "选择" : "",
                    s_record_count > 0U ? "卸载" : "", "返回");
        return;
    }
    if (switching) {
        lv_label_set_text(s_status, "正在启动插件服务");
        set_actions("", "", "");
        return;
    }
    if (radio_error != ESP_OK) {
        lv_label_set_text_fmt(s_status, "蓝牙错误：%s", esp_err_to_name(radio_error));
        set_actions("重试", "", "返回");
        return;
    }

    switch (snapshot.state) {
    case PLUGIN_INSTALL_IDLE:
        switch (plugin_ble_sync_state()) {
        case PLUGIN_BLE_DISCONNECTED:
            lv_label_set_text(s_status, "等待网页连接");
            break;
        case PLUGIN_BLE_CONNECTED:
            lv_label_set_text(s_status, "网页已连接，请同步设备码");
            break;
        case PLUGIN_BLE_SYNCED:
            lv_label_set_text(s_status, "设备已同步，可远程安装");
            break;
        case PLUGIN_BLE_CODE_MISMATCH:
            lv_label_set_text(s_status, "设备码不匹配");
            break;
        }
        set_actions(s_record_count > 0U ? "选择" : "",
                    s_record_count > 0U ? "卸载" : "", "返回");
        break;
    case PLUGIN_INSTALL_RECEIVING:
        lv_label_set_text_fmt(s_status, "接收中 %u / %u",
                              (unsigned)snapshot.received, (unsigned)snapshot.expected);
        set_actions("", "", "取消");
        break;
    case PLUGIN_INSTALL_VERIFYING:
        lv_label_set_text(s_status, "正在验证插件");
        set_actions("", "", "");
        break;
    case PLUGIN_INSTALL_WAITING_APPROVAL:
        lv_label_set_text(s_status, "等待确认安装");
        set_actions("", "安装", "拒绝");
        break;
    case PLUGIN_INSTALL_INSTALLING:
        lv_label_set_text(s_status, "正在安装，请勿断电");
        set_actions("", "", "");
        break;
    case PLUGIN_INSTALL_COMPLETE:
        lv_label_set_text_fmt(s_status, "安装完成：%s", snapshot.installed.manifest.name);
        set_actions("", "继续", "返回");
        break;
    case PLUGIN_INSTALL_ERROR:
        lv_label_set_text_fmt(s_status, "安装失败：%s", esp_err_to_name(snapshot.error));
        set_actions("", "清除", "返回");
        break;
    }
}

static void radio_task(void *argument)
{
    radio_request_t requested;
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_radio.queue, &requested, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t result = ESP_OK;
        if (requested == RADIO_START && !plugin_ble_running()) {
            result = plugin_ble_start();
        } else if (requested == RADIO_STOP && plugin_ble_installer_running()) {
            plugin_ble_stop_installer();
        }
        xSemaphoreTake(s_radio.mutex, portMAX_DELAY);
        if (s_radio.requested == requested) {
            s_radio.error = result;
            s_radio.switching = false;
        }
        xSemaphoreGive(s_radio.mutex);
    }
}

static void request_radio(radio_request_t request)
{
    xSemaphoreTake(s_radio.mutex, portMAX_DELAY);
    s_radio.requested = request;
    s_radio.error = ESP_OK;
    s_radio.switching = true;
    xSemaphoreGive(s_radio.mutex);
    if (xQueueOverwrite(s_radio.queue, &request) != pdTRUE) {
        xSemaphoreTake(s_radio.mutex, portMAX_DELAY);
        s_radio.error = ESP_ERR_NO_MEM;
        s_radio.switching = false;
        xSemaphoreGive(s_radio.mutex);
    }
}

esp_err_t plugin_manager_init(void)
{
    if (s_radio.initialized) return ESP_OK;
    s_radio.mutex = xSemaphoreCreateMutex();
    s_radio.queue = xQueueCreate(1U, sizeof(radio_request_t));
    if (!s_radio.mutex || !s_radio.queue) {
        if (s_radio.queue) vQueueDelete(s_radio.queue);
        if (s_radio.mutex) vSemaphoreDelete(s_radio.mutex);
        s_radio.queue = NULL;
        s_radio.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_radio.requested = RADIO_STOP;
    if (xTaskCreate(radio_task, "plugin_radio", 3584, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_radio.queue);
        vSemaphoreDelete(s_radio.mutex);
        s_radio.queue = NULL;
        s_radio.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_radio.initialized = true;
    return ESP_OK;
}

void plugin_manager_enter(void)
{
    plugin_manager_model_reset(&s_model, 0U);
    s_notice_until = 0;
    s_last_install_state = (plugin_install_state_t)-1;
    load_records();
    build_manager_screen();

    s_refresh_timer = lv_timer_create(refresh_ui, 200U, NULL);
    request_radio(RADIO_START);
    refresh_ui(NULL);
}

void plugin_manager_exit(void)
{
    plugin_installer_snapshot_t snapshot;
    plugin_installer_snapshot(&snapshot);
    if (snapshot.state == PLUGIN_INSTALL_WAITING_APPROVAL) plugin_installer_reject();
    if (snapshot.state == PLUGIN_INSTALL_RECEIVING || snapshot.state == PLUGIN_INSTALL_ERROR ||
        snapshot.state == PLUGIN_INSTALL_COMPLETE) {
        plugin_installer_reset();
    }
    request_radio(RADIO_STOP);
    if (s_refresh_timer) lv_timer_delete(s_refresh_timer);
    if (s_screen) lv_obj_delete(s_screen);
    s_refresh_timer = NULL;
    s_screen = NULL;
    s_status = NULL;
    s_count_label = NULL;
    s_empty_panel = NULL;
    s_action_bar = NULL;
    memset(&s_install_dialog, 0, sizeof(s_install_dialog));
    memset(&s_uninstall_dialog, 0, sizeof(s_uninstall_dialog));
    plugin_manager_model_cancel_remove(&s_model);
    for (size_t row = 0; row < MANAGER_VISIBLE_ROWS; ++row) {
        s_rows[row] = NULL;
        s_row_labels[row] = NULL;
    }
}

void plugin_manager_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    plugin_installer_snapshot_t snapshot;
    esp_err_t radio_error;
    bool switching;

    plugin_installer_snapshot(&snapshot);
    radio_snapshot(&radio_error, &switching);

    if (s_model.confirmation_open) {
        if (event == BSP_BTN_PRESS &&
            (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
            plugin_manager_model_toggle_remove(&s_model);
            refresh_uninstall_options();
        } else if (event == BSP_BTN_CLICK && button == BSP_BTN_OK) {
            plugin_manager_confirm_result_t result =
                plugin_manager_model_confirm(&s_model);
            ESP_LOGI(TAG, "uninstall prompt result=%d id=%s", (int)result,
                     s_uninstall_id);
            if (result == PLUGIN_MANAGER_CONFIRM_REMOVE) uninstall_selected();
            else close_uninstall_dialog();
        }
        refresh_ui(NULL);
        return;
    }

    if (event == BSP_BTN_CLICK && button == BSP_BTN_OK) {
        if (snapshot.state == PLUGIN_INSTALL_WAITING_APPROVAL) {
            plugin_installer_approve();
        } else if (snapshot.state == PLUGIN_INSTALL_COMPLETE ||
                   snapshot.state == PLUGIN_INSTALL_ERROR) {
            plugin_installer_reset();
        } else if (snapshot.state == PLUGIN_INSTALL_IDLE) {
            open_uninstall_dialog();
        }
    } else if (event == BSP_BTN_PRESS && button == BSP_BTN_UP) {
        if (!switching && radio_error != ESP_OK) {
            request_radio(RADIO_START);
        } else if (snapshot.state == PLUGIN_INSTALL_IDLE && s_record_count > 0U) {
            plugin_manager_model_move(&s_model, -1);
            refresh_list();
        }
    } else if (event == BSP_BTN_PRESS && button == BSP_BTN_DOWN &&
               snapshot.state == PLUGIN_INSTALL_IDLE && s_record_count > 0U) {
        plugin_manager_model_move(&s_model, 1);
        refresh_list();
    }
    refresh_ui(NULL);
}

bool plugin_manager_back(void)
{
    plugin_installer_snapshot_t snapshot;
    plugin_installer_snapshot(&snapshot);

    if (s_model.confirmation_open) {
        close_uninstall_dialog();
        refresh_ui(NULL);
        return true;
    }
    if (snapshot.state == PLUGIN_INSTALL_WAITING_APPROVAL) {
        plugin_installer_reject();
        refresh_ui(NULL);
        return true;
    }
    if (snapshot.state == PLUGIN_INSTALL_COMPLETE ||
        snapshot.state == PLUGIN_INSTALL_ERROR ||
        snapshot.state == PLUGIN_INSTALL_RECEIVING) {
        plugin_installer_reset();
        refresh_ui(NULL);
        return true;
    }
    if (snapshot.state == PLUGIN_INSTALL_VERIFYING ||
        snapshot.state == PLUGIN_INSTALL_INSTALLING) {
        show_notice("正在处理，暂不能返回");
        refresh_ui(NULL);
        return true;
    }
    return false;
}
