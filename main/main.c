#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "passport_app_registry.h"
#include "passport_identity.h"
#include "passport_input_policy.h"
#include "passport_link.h"
#include "passport_navigation.h"
#include "passport_package.h"
#include "passport_runtime.h"
#include "passport_settings.h"
#include "passport_storage.h"
#include "passport_theme.h"
#include "passport_ui.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "passport_main";
#define EVENT_QUEUE_DEPTH 16
#define SETTINGS_VALUE_ROW_COUNT 4
#define SETTINGS_LIST_ROW_COUNT (SETTINGS_VALUE_ROW_COUNT + 1)

typedef enum {
    VIEW_LAUNCHER = 0,
    VIEW_PLUGINS,
    VIEW_PLUGIN_DETAIL,
    VIEW_SETTINGS,
    VIEW_DEVICE_INFO,
    VIEW_THEMES,
    VIEW_LUA_APP,
} view_t;

typedef enum {
    EVENT_KEY = 1,
    EVENT_LINK_FRAME,
    EVENT_PACKAGE_INSTALLED,
    EVENT_STORAGE_COMPLETED,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        struct { bsp_btn_t btn; bsp_btn_ev_t ev; } key;
        struct {
            passport_link_frame_t frame;
            uint8_t payload[PASSPORT_LINK_MAX_PAYLOAD];
        } link;
        passport_app_storage_completion_t *storage;
    } data;
} system_event_t;

static QueueHandle_t s_events;
static view_t s_view;
static passport_navigation_t s_navigation;
static passport_page_t *s_page;
static passport_ui_list_t *s_list;
static size_t s_plugin_detail_index;
static bool s_plugin_uninstall_armed;
static bool s_plugin_uninstall_pending;
static uint32_t s_system_storage_request_id;
static lv_obj_t *s_plugin_detail_notice;
static passport_theme_info_t s_themes[PASSPORT_MAX_INSTALLED_THEMES];
static size_t s_theme_count;
static passport_settings_wake_guard_t s_wake_guard;

static void on_storage_completion(
    passport_app_storage_completion_t *completion, void *user);

static void finish_native_page(const char *ok_action)
{
    if (ok_action) passport_ui_page_set_action(s_page, ok_action);
    passport_ui_page_set_can_go_back(
        s_page, passport_navigation_can_pop(&s_navigation));
    passport_ui_page_show(s_page);
}

static void restore_list_selection(void)
{
    const passport_navigation_frame_t *frame = passport_navigation_current(&s_navigation);
    if (s_list && frame && frame->state > 0) {
        passport_ui_list_move(s_list, frame->state);
    }
}

static const passport_setting_id_t SETTINGS_ROWS[SETTINGS_VALUE_ROW_COUNT] = {
    PASSPORT_SETTING_BRIGHTNESS,
    PASSPORT_SETTING_SCREEN_TIMEOUT,
    PASSPORT_SETTING_VOLUME,
    PASSPORT_SETTING_KEY_SOUND,
};

static const char *const SETTINGS_NAMES[SETTINGS_VALUE_ROW_COUNT] = {
    "屏幕亮度",
    "息屏时间",
    "系统音量",
    "按键音",
};

static void destroy_native_view(void)
{
    if (s_list) {
        passport_ui_list_destroy(s_list);
        s_list = NULL;
    }
    if (s_page) {
        passport_ui_page_destroy(s_page);
        s_page = NULL;
    }
    s_plugin_detail_notice = NULL;
}

static void show_launcher(void)
{
    if (passport_runtime_running()) passport_runtime_stop();
    destroy_native_view();
    passport_app_registry_scan();

    s_page = passport_ui_page_create("Passport", true, true);
    s_list = passport_ui_list_create(s_page, 3 + PASSPORT_MAX_INSTALLED_APPS);
    passport_ui_list_add(s_list, "插件管理");
    passport_ui_list_add(s_list, "设置");
    passport_ui_list_add(s_list, "主题");
    for (size_t i = 0; i < passport_app_registry_count(); ++i) {
        const passport_app_info_t *app = passport_app_registry_get(i);
        if (app) passport_ui_list_add(s_list, app->manifest.name);
    }
    restore_list_selection();
    finish_native_page("打开");
    s_view = VIEW_LAUNCHER;
}

static void show_plugins(void)
{
    destroy_native_view();
    passport_app_registry_scan();
    s_page = passport_ui_page_create("插件管理", true, true);
    char info[96];
    snprintf(info, sizeof(info), "设备码 %s\n用于网页连接与安装", passport_identity_code());
    passport_ui_label_create(s_page, info);
    s_list = passport_ui_list_create(s_page, PASSPORT_MAX_INSTALLED_APPS);
    for (size_t i = 0; i < passport_app_registry_count(); ++i) {
        const passport_app_info_t *app = passport_app_registry_get(i);
        char row[80];
        if (!app) continue;
        snprintf(row, sizeof(row), "%s  %s", app->manifest.name, app->manifest.version);
        passport_ui_list_add(s_list, row);
    }
    restore_list_selection();
    finish_native_page(passport_app_registry_count() > 0 ? "详情" : "");
    s_view = VIEW_PLUGINS;
}

static void show_plugin_detail(size_t app_index)
{
    const passport_app_info_t *app = passport_app_registry_get(app_index);
    if (!app) return;
    destroy_native_view();
    s_plugin_detail_index = app_index;
    s_plugin_uninstall_armed = false;
    s_plugin_uninstall_pending = false;
    s_page = passport_ui_page_create("插件详情", true, true);
    char line[160];
    snprintf(line, sizeof(line), "%s\n版本：%s\n标识：%s", app->manifest.name,
             app->manifest.version, app->manifest.id);
    passport_ui_label_create(s_page, line);
    s_plugin_detail_notice = passport_ui_label_create(s_page, "");
    s_list = passport_ui_list_create(s_page, 2);
    passport_ui_list_add(s_list, "返回插件列表");
    passport_ui_list_add(s_list, "卸载插件");
    finish_native_page("打开");
    s_view = VIEW_PLUGIN_DETAIL;
}

static void format_setting_value(passport_setting_id_t id,
                                 uint16_t value,
                                 char *out,
                                 size_t capacity)
{
    if (id == PASSPORT_SETTING_BRIGHTNESS || id == PASSPORT_SETTING_VOLUME) {
        snprintf(out, capacity, "%u%%", (unsigned)value);
    } else if (id == PASSPORT_SETTING_KEY_SOUND) {
        snprintf(out, capacity, "%s", value ? "开启" : "关闭");
    } else if (value == 0U) {
        snprintf(out, capacity, "从不");
    } else if (value < 60U) {
        snprintf(out, capacity, "%u 秒", (unsigned)value);
    } else {
        snprintf(out, capacity, "%u 分钟", (unsigned)(value / 60U));
    }
}

static void refresh_settings(void)
{
    if (!s_page || !s_list) return;
    for (size_t i = 0; i < SETTINGS_VALUE_ROW_COUNT; ++i) {
        uint16_t value = 0U;
        char text[24];
        if (!passport_settings_get(SETTINGS_ROWS[i], &value)) continue;
        format_setting_value(SETTINGS_ROWS[i], value, text, sizeof(text));
        passport_ui_list_set_value(s_list, i, text);
    }
    const size_t selected = passport_ui_list_selected(s_list);
    const char *action = "调整";
    if (selected == SETTINGS_VALUE_ROW_COUNT) action = "查看";
    else if (selected < SETTINGS_VALUE_ROW_COUNT &&
             SETTINGS_ROWS[selected] == PASSPORT_SETTING_KEY_SOUND) action = "切换";
    passport_ui_page_set_action(s_page, action);
}

static void show_settings(void)
{
    destroy_native_view();
    s_page = passport_ui_page_create("设置", true, true);
    s_list = passport_ui_list_create(s_page, SETTINGS_LIST_ROW_COUNT);
    for (size_t i = 0; i < SETTINGS_VALUE_ROW_COUNT; ++i) {
        passport_ui_list_add_value(s_list, SETTINGS_NAMES[i], "");
    }
    passport_ui_list_add(s_list, "设备信息");
    restore_list_selection();
    refresh_settings();
    finish_native_page(NULL);
    s_view = VIEW_SETTINGS;
}

static void show_device_info(void)
{
    destroy_native_view();
    s_page = passport_ui_page_create("设备信息", true, true);
    char info[112];
    snprintf(info, sizeof(info), "本机设备码\n%s\n\n用于连接和安装插件",
             passport_identity_code());
    passport_ui_label_create(s_page, info);
    finish_native_page("返回");
    s_view = VIEW_DEVICE_INFO;
}

static void show_themes(void)
{
    destroy_native_view();
    s_theme_count = passport_theme_list(s_themes, PASSPORT_MAX_INSTALLED_THEMES);
    s_page = passport_ui_page_create("主题", true, true);
    s_list = passport_ui_list_create(s_page, PASSPORT_MAX_INSTALLED_THEMES);
    for (size_t i = 0; i < s_theme_count; ++i) {
        char row[80];
        snprintf(row, sizeof(row), "%s%s", s_themes[i].name,
                 strcmp(s_themes[i].id, passport_theme_current_id()) == 0 ? "  当前" : "");
        passport_ui_list_add(s_list, row);
    }
    restore_list_selection();
    finish_native_page("应用");
    s_view = VIEW_THEMES;
}

static bool view_preserves_selection(view_t view)
{
    return view == VIEW_LAUNCHER || view == VIEW_PLUGINS ||
           view == VIEW_SETTINGS || view == VIEW_THEMES;
}

static void save_native_selection(void)
{
    if (s_list && view_preserves_selection(s_view)) {
        passport_navigation_set_state(
            &s_navigation, (int32_t)passport_ui_list_selected(s_list));
    }
}

static void render_native_route(void)
{
    const passport_navigation_frame_t *frame = passport_navigation_current(&s_navigation);
    if (!frame) return;
    switch ((view_t)frame->route) {
    case VIEW_LAUNCHER: show_launcher(); break;
    case VIEW_PLUGINS: show_plugins(); break;
    case VIEW_PLUGIN_DETAIL: show_plugin_detail((size_t)frame->state); break;
    case VIEW_SETTINGS: show_settings(); break;
    case VIEW_DEVICE_INFO: show_device_info(); break;
    case VIEW_THEMES: show_themes(); break;
    default: break;
    }
}

static void native_reset(view_t root)
{
    passport_navigation_reset(&s_navigation, (uint32_t)root, 0);
    render_native_route();
}

static bool native_push(view_t view, int32_t state)
{
    save_native_selection();
    if (!passport_navigation_push(&s_navigation, (uint32_t)view, state)) return false;
    render_native_route();
    return true;
}

static bool native_pop(void)
{
    if (!passport_navigation_pop(&s_navigation)) return false;
    render_native_route();
    return true;
}

static void native_refresh(void)
{
    save_native_selection();
    render_native_route();
}

static void launch_selected_plugin(size_t registry_index)
{
    const passport_app_info_t *app = passport_app_registry_get(registry_index);
    if (!app) return;
    destroy_native_view();
    esp_err_t err = passport_runtime_start(app);
    if (err == ESP_OK) {
        s_view = VIEW_LUA_APP;
        return;
    }
    ESP_LOGE(TAG, "启动插件失败 %s: %s", app->manifest.id, esp_err_to_name(err));
    native_reset(VIEW_LAUNCHER);
}

static void handle_launcher_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_list) return;
    const int delta = passport_input_navigation_delta(btn, ev);
    if (delta != 0) passport_ui_list_move(s_list, delta);
    else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        size_t selected = passport_ui_list_selected(s_list);
        if (selected == 0) native_push(VIEW_PLUGINS, 0);
        else if (selected == 1) native_push(VIEW_SETTINGS, 0);
        else if (selected == 2) native_push(VIEW_THEMES, 0);
        else launch_selected_plugin(selected - 3);
    }
}

static void handle_plugins_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_list) return;
    const int delta = passport_input_navigation_delta(btn, ev);
    if (delta != 0) passport_ui_list_move(s_list, delta);
    else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK &&
             passport_app_registry_count() > 0) {
        size_t selected = passport_ui_list_selected(s_list);
        native_push(VIEW_PLUGIN_DETAIL, (int32_t)selected);
    }
}

static void handle_plugin_detail_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_list) return;
    if (s_plugin_uninstall_pending) return;
    const int delta = passport_input_navigation_delta(btn, ev);
    if (delta != 0) {
        passport_ui_list_move(s_list, delta);
        if (s_plugin_uninstall_armed) {
            s_plugin_uninstall_armed = false;
            passport_ui_label_set_text(s_plugin_detail_notice, "");
            passport_ui_page_set_action(s_page, "打开");
        }
        return;
    }
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;
    if (passport_ui_list_selected(s_list) == 0) {
        native_pop();
        return;
    }
    const passport_app_info_t *app = passport_app_registry_get(s_plugin_detail_index);
    if (!app) { native_pop(); return; }
    if (!s_plugin_uninstall_armed) {
        s_plugin_uninstall_armed = true;
        passport_ui_page_set_action(s_page, "确认卸载");
        passport_ui_label_set_text(s_plugin_detail_notice, "再按一次确定卸载");
        return;
    }
    char id[PASSPORT_MANIFEST_ID_MAX];
    snprintf(id, sizeof(id), "%s", app->manifest.id);
    ++s_system_storage_request_id;
    if (s_system_storage_request_id == 0U) ++s_system_storage_request_id;
    passport_app_storage_error_t error = passport_app_storage_uninstall_async(
        id, s_system_storage_request_id, on_storage_completion, NULL);
    if (error != PASSPORT_APP_STORAGE_OK) {
        ESP_LOGE(TAG, "无法提交卸载 %s: %u", id, (unsigned)error);
        passport_ui_label_set_text(s_plugin_detail_notice, "卸载任务提交失败，请重试");
        passport_ui_page_set_action(s_page, "重试卸载");
        s_plugin_uninstall_armed = false;
        return;
    }
    s_plugin_uninstall_pending = true;
    passport_ui_label_set_text(s_plugin_detail_notice, "正在删除插件及其数据…");
    passport_ui_page_set_action(s_page, "卸载中");
}

static void handle_themes_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_list || s_theme_count == 0) return;
    const int delta = passport_input_navigation_delta(btn, ev);
    if (delta != 0) passport_ui_list_move(s_list, delta);
    else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        size_t selected = passport_ui_list_selected(s_list);
        if (selected < s_theme_count &&
            passport_theme_apply(s_themes[selected].id) == ESP_OK) {
            passport_ui_theme_refresh();
            native_refresh();
        }
    }
}

static void handle_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_list) return;
    const int delta = passport_input_navigation_delta(btn, ev);
    if (delta != 0) {
        passport_ui_list_move(s_list, delta);
        refresh_settings();
        return;
    }
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;
    const size_t selected = passport_ui_list_selected(s_list);
    if (selected == SETTINGS_VALUE_ROW_COUNT) {
        native_push(VIEW_DEVICE_INFO, 0);
        return;
    }
    if (selected >= SETTINGS_VALUE_ROW_COUNT) return;
    esp_err_t err = passport_settings_cycle(SETTINGS_ROWS[selected]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "修改设置失败: %s", esp_err_to_name(err));
        return;
    }
    if (SETTINGS_ROWS[selected] == PASSPORT_SETTING_VOLUME) {
        passport_settings_sound_preview();
    }
    refresh_settings();
}

static void handle_device_info_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) native_pop();
}

static bool consume_screen_wake(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    const bool woke = passport_settings_note_activity();
    const bool terminal = ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE ||
                          ev == BSP_BTN_LONG;
    return passport_settings_model_consume_wake(
        &s_wake_guard, (uint8_t)btn, ev == BSP_BTN_PRESS, terminal, woke);
}

static bool is_key_sound_event(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    return ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE ||
           (btn == BSP_BTN_OK && ev == BSP_BTN_LONG);
}

static void handle_key_event(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    /* 长按确定由导航组件统一消费，不交给 PAP。 */
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (s_plugin_uninstall_pending) return;
        if (s_view == VIEW_LUA_APP && passport_runtime_navigate_back()) return;
        if (s_view != VIEW_LUA_APP && native_pop()) return;
        native_reset(VIEW_LAUNCHER);
        return;
    }
    switch (s_view) {
    case VIEW_LAUNCHER: handle_launcher_key(btn, ev); break;
    case VIEW_PLUGINS: handle_plugins_key(btn, ev); break;
    case VIEW_PLUGIN_DETAIL: handle_plugin_detail_key(btn, ev); break;
    case VIEW_SETTINGS: handle_settings_key(btn, ev); break;
    case VIEW_DEVICE_INFO: handle_device_info_key(btn, ev); break;
    case VIEW_THEMES: handle_themes_key(btn, ev); break;
    case VIEW_LUA_APP: passport_runtime_handle_key(btn, ev); break;
    default: break;
    }
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_events) return;
    system_event_t event = {.type = EVENT_KEY};
    event.data.key.btn = btn;
    event.data.key.ev = ev;
    xQueueSend(s_events, &event, 0);
}

static void on_link_frame(const passport_link_frame_t *frame, void *user)
{
    (void)user;
    if (!s_events || !frame || frame->payload_len > PASSPORT_LINK_MAX_PAYLOAD) return;
    system_event_t event = {.type = EVENT_LINK_FRAME};
    event.data.link.frame = *frame;
    if (frame->payload_len) memcpy(event.data.link.payload, frame->payload, frame->payload_len);
    event.data.link.frame.payload = NULL;
    xQueueSend(s_events, &event, 0);
}

static void on_package_install(esp_err_t result, const passport_package_result_t *package, void *user)
{
    (void)result;
    (void)package;
    (void)user;
    if (!s_events) return;
    system_event_t event = {.type = EVENT_PACKAGE_INSTALLED};
    xQueueSend(s_events, &event, 0);
}

static void on_storage_completion(
    passport_app_storage_completion_t *completion, void *user)
{
    (void)user;
    if (!s_events) {
        passport_app_storage_completion_free(completion);
        return;
    }
    system_event_t event = {.type = EVENT_STORAGE_COMPLETED};
    event.data.storage = completion;
    xQueueSend(s_events, &event, portMAX_DELAY);
}

static void handle_storage_completion(
    passport_app_storage_completion_t *completion)
{
    if (!completion) return;
    if (completion->operation != PASSPORT_APP_STORAGE_UNINSTALL) {
        passport_runtime_handle_storage_completion(completion);
        return;
    }

    s_plugin_uninstall_pending = false;
    if (completion->error == PASSPORT_APP_STORAGE_OK) {
        ESP_LOGI(TAG, "卸载 %s: 完成", completion->app_id);
        passport_app_storage_completion_free(completion);
        if (s_view == VIEW_PLUGIN_DETAIL) native_pop();
        return;
    }
    ESP_LOGE(TAG, "卸载 %s 失败: %u", completion->app_id,
             (unsigned)completion->error);
    passport_app_storage_completion_free(completion);
    s_plugin_uninstall_armed = false;
    if (s_view == VIEW_PLUGIN_DETAIL && s_plugin_detail_notice) {
        passport_ui_label_set_text(s_plugin_detail_notice, "卸载失败，请重试");
        passport_ui_page_set_action(s_page, "重试卸载");
    }
}

static void system_task(void *arg)
{
    (void)arg;
    system_event_t event;
    while (xQueueReceive(s_events, &event, portMAX_DELAY) == pdTRUE) {
        if (event.type == EVENT_KEY &&
            consume_screen_wake(event.data.key.btn, event.data.key.ev)) {
            continue;
        }
        if (!bsp_lvgl_lock(1000)) {
            if (event.type == EVENT_STORAGE_COMPLETED) {
                xQueueSendToFront(s_events, &event, portMAX_DELAY);
                vTaskDelay(1);
            }
            continue;
        }
        if (event.type == EVENT_KEY) {
            handle_key_event(event.data.key.btn, event.data.key.ev);
        } else if (event.type == EVENT_LINK_FRAME && s_view == VIEW_LUA_APP) {
            event.data.link.frame.payload = event.data.link.payload;
            passport_runtime_handle_link(&event.data.link.frame);
        } else if (event.type == EVENT_PACKAGE_INSTALLED) {
            if (s_view == VIEW_PLUGINS || s_view == VIEW_THEMES) native_refresh();
        } else if (event.type == EVENT_STORAGE_COMPLETED) {
            handle_storage_completion(event.data.storage);
        }
        bsp_lvgl_unlock();
        if (event.type == EVENT_KEY &&
            is_key_sound_event(event.data.key.btn, event.data.key.ev)) {
            passport_settings_key_feedback();
        }
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Passport Platform v1 启动");
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(passport_identity_init());
    ESP_ERROR_CHECK(bsp_i2c_init());

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败，系统无法启动");
        return;
    }
    esp_err_t settings_err = passport_settings_init();
    if (settings_err != ESP_OK) {
        ESP_LOGE(TAG, "设置服务初始化不完整: %s", esp_err_to_name(settings_err));
    }

    esp_err_t storage_err = passport_storage_init();
    if (storage_err != ESP_OK) ESP_LOGE(TAG, "插件存储不可用: %s", esp_err_to_name(storage_err));
    bool battery_ok = bsp_battery_init() == ESP_OK;
    passport_theme_init();
    passport_ui_init(battery_ok);

    s_events = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(system_event_t));
    if (!s_events) {
        ESP_LOGE(TAG, "系统事件队列创建失败");
        return;
    }
    passport_runtime_storage_set_dispatcher(on_storage_completion, NULL);
    if (xTaskCreate(system_task, "passport_system", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "系统任务创建失败");
        return;
    }

    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    passport_link_set_rx_callback(on_link_frame, NULL);
    passport_link_set_install_callback(on_package_install, NULL);
    esp_err_t link_err = passport_link_init();
    if (link_err != ESP_OK) ESP_LOGE(TAG, "Passport Link 启动失败: %s", esp_err_to_name(link_err));

    if (bsp_lvgl_lock(1000)) {
        native_reset(VIEW_LAUNCHER);
        bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG, "系统就绪，设备码=%s", passport_identity_code());
}
