// Product shell: a Cordis-inspired registry makes built-in capabilities and
// downloaded packages follow one lifecycle. The home screen is the registry view.
#include "app_registry.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "device_identity.h"
#include "device_settings.h"
#include "plugin_manager.h"
#include "plugin_host.h"
#include "plugin_installer.h"
#include "nearby_service.h"
#include "system_plugins.h"
#include "ui_pixel.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <stddef.h>
#include <stdatomic.h>

#define HOME_QUICK_COUNT 2U
#define HOME_VISIBLE_ROWS 4U
#define INPUT_QUEUE_DEPTH 32U

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_event_t;

static const char *TAG = "main";
static lv_obj_t *s_home_screen;
static lv_obj_t *s_quick_panels[HOME_QUICK_COUNT];
static lv_obj_t *s_quick_icons[HOME_QUICK_COUNT];
static lv_obj_t *s_quick_names[HOME_QUICK_COUNT];
static lv_obj_t *s_plugin_rows[HOME_VISIBLE_ROWS];
static lv_obj_t *s_plugin_labels[HOME_VISIBLE_ROWS];
static lv_obj_t *s_plugin_count;
static lv_obj_t *s_empty_panel;
static size_t s_selected;
static QueueHandle_t s_input_queue;
static _Atomic uint32_t s_dropped_inputs;
static bool s_suppress_wake_ok;

static bool is_action_event(bsp_btn_t button, bsp_btn_ev_t event)
{
    return (event == BSP_BTN_PRESS &&
            (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) ||
           (button == BSP_BTN_OK &&
            (event == BSP_BTN_CLICK || event == BSP_BTN_LONG));
}

static void set_entry_style(lv_obj_t *panel, lv_obj_t *label,
                            bool selected, bool available)
{
    ui_pixel_set_selected(panel, selected, available);
    lv_obj_set_style_text_color(label,
        lv_color_hex(available ? UI_INK : UI_TEXT_MUTED), 0);
}

static void home_refresh(void)
{
    const size_t count = app_registry_count();
    const size_t pinned = app_registry_pinned_count();
    const size_t package_count = count > pinned ? count - pinned : 0U;

    if (count == 0U) return;
    if (s_selected >= count) s_selected = 0U;

    for (size_t index = 0; index < HOME_QUICK_COUNT; ++index) {
        const app_plugin_info_t *entry = app_registry_get(index);
        if (!entry) continue;
        lv_label_set_text(s_quick_icons[index], entry->icon);
        lv_label_set_text(s_quick_names[index], entry->name);
        set_entry_style(s_quick_panels[index], s_quick_names[index],
                        s_selected == index, entry->available);
        lv_obj_set_style_text_color(s_quick_icons[index],
            lv_color_hex(entry->available ? UI_INK : UI_TEXT_MUTED), 0);
    }

    lv_label_set_text_fmt(s_plugin_count, "%u 个", (unsigned)package_count);
    if (package_count == 0U) {
        lv_obj_remove_flag(s_empty_panel, LV_OBJ_FLAG_HIDDEN);
        for (size_t row = 0; row < HOME_VISIBLE_ROWS; ++row) {
            lv_obj_add_flag(s_plugin_rows[row], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(s_empty_panel, LV_OBJ_FLAG_HIDDEN);

    size_t selected_package = s_selected >= pinned ? s_selected - pinned : 0U;
    size_t window = 0U;
    if (package_count > HOME_VISIBLE_ROWS && selected_package >= HOME_VISIBLE_ROWS) {
        window = selected_package - HOME_VISIBLE_ROWS + 1U;
    }
    if (package_count > HOME_VISIBLE_ROWS &&
        window + HOME_VISIBLE_ROWS > package_count) {
        window = package_count - HOME_VISIBLE_ROWS;
    }
    for (size_t row = 0; row < HOME_VISIBLE_ROWS; ++row) {
        size_t package_index = window + row;
        if (package_index >= package_count) {
            lv_obj_add_flag(s_plugin_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        size_t entry_index = pinned + package_index;
        const app_plugin_info_t *entry = app_registry_get(entry_index);
        lv_obj_remove_flag(s_plugin_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_plugin_labels[row], "%s  %s", entry->icon, entry->name);
        set_entry_style(s_plugin_rows[row], s_plugin_labels[row],
                        s_selected == entry_index, entry->available);
    }
}

static void home_build(void)
{
    app_registry_refresh();
    s_home_screen = ui_pixel_screen_create("主页");

    for (size_t index = 0; index < HOME_QUICK_COUNT; ++index) {
        int x = 8 + (int)index * 116;
        s_quick_panels[index] = ui_pixel_panel_create(
            s_home_screen, x, 38, 108, 52, UI_PAPER);
        s_quick_icons[index] = ui_pixel_label(
            s_quick_panels[index], "", UI_FONT_TITLE, UI_INK);
        lv_obj_align(s_quick_icons[index], LV_ALIGN_TOP_MID, 0, -4);
        s_quick_names[index] = ui_pixel_label(
            s_quick_panels[index], "", UI_FONT_BODY, UI_INK);
        lv_obj_align(s_quick_names[index], LV_ALIGN_BOTTOM_MID, 0, 3);
    }

    lv_obj_t *heading = ui_pixel_label(
        s_home_screen, "插件", UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(heading, 10, 100);
    s_plugin_count = ui_pixel_label(
        s_home_screen, "", UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(s_plugin_count, 126, 100);
    lv_obj_set_width(s_plugin_count, 104);
    lv_obj_set_style_text_align(s_plugin_count, LV_TEXT_ALIGN_RIGHT, 0);

    for (size_t row = 0; row < HOME_VISIBLE_ROWS; ++row) {
        s_plugin_rows[row] = ui_pixel_panel_create(
            s_home_screen, 10, 120 + (int)row * 41, 220, 35, UI_PAPER);
        s_plugin_labels[row] = ui_pixel_label(
            s_plugin_rows[row], "", UI_FONT_BODY, UI_INK);
        lv_obj_set_width(s_plugin_labels[row], 190);
        lv_label_set_long_mode(s_plugin_labels[row], LV_LABEL_LONG_DOT);
        lv_obj_align(s_plugin_labels[row], LV_ALIGN_LEFT_MID, 3, 0);
    }

    s_empty_panel = ui_pixel_empty_state(
        s_home_screen, 10, 120, 220, 76, "暂无插件\n请从插件页安装");

    ui_pixel_action_bar(s_home_screen, "选择", "打开", "");

    home_refresh();
    lv_screen_load(s_home_screen);
}

static void return_home(void)
{
    app_registry_exit();
    home_build();
}

static void open_selected(void)
{
    const app_plugin_info_t *entry = app_registry_get(s_selected);
    if (!entry || !entry->available) return;

    lv_obj_delete(s_home_screen);
    s_home_screen = NULL;
    esp_err_t result = app_registry_enter(s_selected);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "cannot open %s: %s", entry->id, esp_err_to_name(result));
        home_build();
    }
}

static void handle_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (s_suppress_wake_ok && button == BSP_BTN_OK &&
        (event == BSP_BTN_CLICK || event == BSP_BTN_LONG)) {
        s_suppress_wake_ok = false;
        device_settings_note_activity();
        return;
    }
    if (device_settings_note_activity()) {
        if (button == BSP_BTN_OK && event == BSP_BTN_PRESS) {
            s_suppress_wake_ok = true;
        }
        return;
    }
    if (!bsp_lvgl_lock(500)) return;

    if (app_registry_active()) {
        app_registry_key(button, event);
        if (app_registry_take_home_request()) return_home();
    } else if (s_home_screen) {
        size_t count = app_registry_count();
        if (count > 0U && event == BSP_BTN_PRESS && button == BSP_BTN_UP) {
            s_selected = (s_selected + count - 1U) % count;
            home_refresh();
        } else if (count > 0U && event == BSP_BTN_PRESS && button == BSP_BTN_DOWN) {
            s_selected = (s_selected + 1U) % count;
            home_refresh();
        } else if (event == BSP_BTN_CLICK && button == BSP_BTN_OK) {
            open_selected();
        }
    }
    bsp_lvgl_unlock();
    if (is_action_event(button, event)) device_settings_key_feedback();
}

static void input_task(void *argument)
{
    input_event_t input;
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            uint32_t dropped = atomic_exchange_explicit(
                &s_dropped_inputs, 0U, memory_order_relaxed);
            if (dropped > 0U) ESP_LOGW(TAG, "input queue dropped %u events", (unsigned)dropped);
            handle_key(input.button, input.event);
        }
    }
}

// The button timer task must remain free to sample the next physical press.
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    input_event_t input = {
        .button = button,
        .event = event,
    };
    (void)user;

    if (xQueueSend(s_input_queue, &input, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_dropped_inputs, 1U, memory_order_relaxed);
    }
}

static esp_err_t input_system_init(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "app_input", 3584, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_main(void)
{
    uint32_t services = 0U;

    ESP_LOGI(TAG, "Passport plugin shell v2.6.0 starting");
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed without erasing existing data: %s",
                 esp_err_to_name(nvs_result));
    }

    bsp_i2c_init();
    bsp_i2c_scan();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display unavailable (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100U);
    services |= APP_SERVICE_DISPLAY;

    bool audio_available = bsp_audio_init() == ESP_OK;
    bool battery_available = bsp_battery_init() == ESP_OK;
    if (audio_available) services |= APP_SERVICE_AUDIO;
    esp_err_t settings_result = device_settings_init(audio_available);
    if (settings_result == ESP_OK) services |= APP_SERVICE_SETTINGS;
    else ESP_LOGE(TAG, "settings service unavailable: %s",
                  esp_err_to_name(settings_result));
    plugin_host_system_init(audio_available);

    esp_err_t identity_result = device_identity_init();
    if (identity_result == ESP_OK) services |= APP_SERVICE_IDENTITY;
    else ESP_LOGE(TAG, "device identity unavailable: %s", esp_err_to_name(identity_result));

    esp_err_t installer_result = plugin_installer_init();
    if (installer_result == ESP_OK) services |= APP_SERVICE_STORAGE;
    else ESP_LOGE(TAG, "plugin store unavailable: %s", esp_err_to_name(installer_result));
    if (installer_result == ESP_OK) ui_theme_init();

    esp_err_t nearby_result = nearby_service_system_init(audio_available);
    if (nearby_result != ESP_OK) {
        ESP_LOGE(TAG, "nearby service unavailable: %s",
                 esp_err_to_name(nearby_result));
    }

    esp_err_t manager_result = plugin_manager_init();
    if (installer_result == ESP_OK && identity_result == ESP_OK &&
        nearby_result == ESP_OK && manager_result == ESP_OK) {
        services |= APP_SERVICE_NEARBY;
    } else if (manager_result != ESP_OK) {
        ESP_LOGE(TAG, "plugin manager unavailable: %s", esp_err_to_name(manager_result));
    }

    esp_err_t system_plugin_result = system_plugins_init(battery_available);
    if (system_plugin_result != ESP_OK) {
        ESP_LOGW(TAG, "system plugin unavailable: %s",
                 esp_err_to_name(system_plugin_result));
    }
    app_registry_init(services);
    esp_err_t input_result = input_system_init();

    if (bsp_lvgl_lock(1000)) {
        home_build();
        bsp_lvgl_unlock();
    }
    bool buttons_available = input_result == ESP_OK &&
                             bsp_button_init(on_key, NULL) == ESP_OK;

    ESP_LOGI(TAG, "ready: buttons=%d audio=%d battery=%d plugins=%u code=%s",
             buttons_available, audio_available, battery_available,
             (unsigned)(app_registry_count() - app_registry_pinned_count()),
             device_identity_code());
    ESP_LOGI(TAG, "8-bit heap: free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
