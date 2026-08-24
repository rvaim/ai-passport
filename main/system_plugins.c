#include "system_plugins.h"

#include "device_identity.h"
#include "device_settings.h"
#include "plugin_format.h"
#include "ui_pixel.h"
#include "ui_theme.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

#include <stdio.h>

#define SETTINGS_ROW_COUNT (PLUGIN_SETTING_COUNT + 1U)
#define SETTINGS_DEVICE_INDEX PLUGIN_SETTING_COUNT

typedef enum {
    SETTINGS_VIEW_LIST,
    SETTINGS_VIEW_DEVICE,
} settings_view_t;

static lv_obj_t *s_settings_screen;
static lv_obj_t *s_setting_rows[SETTINGS_ROW_COUNT];
static lv_obj_t *s_setting_values[SETTINGS_ROW_COUNT];
static size_t s_setting_selected;
static settings_view_t s_settings_view;

esp_err_t system_plugins_init(bool battery_available)
{
    (void)battery_available;
    return ESP_OK;
}

static const char *timeout_text(int32_t seconds)
{
    if (seconds == 30) return "30 秒";
    if (seconds == 60) return "1 分钟";
    if (seconds == 180) return "3 分钟";
    if (seconds == 300) return "5 分钟";
    return "从不";
}

static void clear_settings_screen(void)
{
    if (s_settings_screen) lv_obj_delete(s_settings_screen);
    s_settings_screen = NULL;
    for (size_t index = 0; index < SETTINGS_ROW_COUNT; ++index) {
        s_setting_rows[index] = NULL;
        s_setting_values[index] = NULL;
    }
}

lv_obj_t *system_device_info_screen_create(void)
{
    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    char details[128];

    esp_chip_info(&chip);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) flash_size = 0;

    lv_obj_t *screen = ui_pixel_screen_create("设备信息");
    lv_obj_t *code_panel = ui_pixel_panel_create(screen, 12, 46, 216, 80, UI_YELLOW);
    lv_obj_t *code_label = ui_pixel_label(
        code_panel, device_identity_code(), &lv_font_montserrat_20, UI_INK);
    lv_obj_align(code_label, LV_ALIGN_CENTER, 0, -7);
    lv_obj_t *code_hint = ui_pixel_label(code_panel, "设备码", UI_FONT_BODY, UI_INK);
    lv_obj_align(code_hint, LV_ALIGN_BOTTOM_MID, 0, -7);

    lv_obj_t *info_panel = ui_pixel_panel_create(screen, 12, 143, 216, 82, UI_PAPER);
    snprintf(details, sizeof(details),
             "ESP32-C3 rev %u.%u  %u 核\n闪存 %lu MB  可用内存 %lu KB",
             (unsigned)(chip.revision / 100U),
             (unsigned)(chip.revision % 100U), (unsigned)chip.cores,
             (unsigned long)(flash_size / (1024U * 1024U)),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024U));
    lv_obj_t *details_label = ui_pixel_label(info_panel, details, UI_FONT_BODY, UI_INK);
    lv_obj_set_style_text_align(details_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(details_label);

    ui_pixel_action_bar(screen, "", "", "返回");
    return screen;
}

static void refresh_settings(void)
{
    int32_t values[PLUGIN_SETTING_COUNT] = {0};
    if (s_settings_view != SETTINGS_VIEW_LIST) return;

    for (uint8_t index = 0; index < PLUGIN_SETTING_THEME; ++index) {
        device_settings_get(index, &values[index]);
    }
    values[PLUGIN_SETTING_THEME] = (int32_t)ui_theme_active_index();
    for (size_t index = 0; index < SETTINGS_ROW_COUNT; ++index) {
        if (s_setting_rows[index]) {
            ui_pixel_set_selected(s_setting_rows[index], index == s_setting_selected, true);
        }
    }
    if (s_setting_values[0]) lv_label_set_text_fmt(s_setting_values[0], "%ld%%", (long)values[0]);
    if (s_setting_values[1]) lv_label_set_text_fmt(s_setting_values[1], "%ld%%", (long)values[1]);
    if (s_setting_values[2]) lv_label_set_text(s_setting_values[2], values[2] ? "开启" : "关闭");
    if (s_setting_values[3]) lv_label_set_text(s_setting_values[3], timeout_text(values[3]));
    if (s_setting_values[PLUGIN_SETTING_THEME]) {
        lv_label_set_text(s_setting_values[PLUGIN_SETTING_THEME],
                          ui_theme_name((size_t)values[PLUGIN_SETTING_THEME]));
    }
    if (s_setting_values[SETTINGS_DEVICE_INDEX]) {
        lv_label_set_text(s_setting_values[SETTINGS_DEVICE_INDEX], "信息");
    }
}

static void build_settings_list(bool reset_selection)
{
    static const char *const names[SETTINGS_ROW_COUNT] = {
        LV_SYMBOL_EYE_OPEN "  亮度",
        LV_SYMBOL_VOLUME_MAX "  音量",
        LV_SYMBOL_BELL "  按键音",
        LV_SYMBOL_POWER "  自动息屏",
        LV_SYMBOL_TINT "  主题",
        LV_SYMBOL_USB "  设备",
    };

    clear_settings_screen();
    if (reset_selection) s_setting_selected = 0U;
    s_settings_view = SETTINGS_VIEW_LIST;
    s_settings_screen = ui_pixel_screen_create("设置");
    size_t window = s_setting_selected >= 5U ? s_setting_selected - 4U : 0U;
    if (window + 5U > SETTINGS_ROW_COUNT) window = SETTINGS_ROW_COUNT - 5U;
    for (size_t row = 0; row < 5U; ++row) {
        size_t index = window + row;
        s_setting_rows[index] = ui_pixel_row_create(
            s_settings_screen, 10, 39 + (int)row * 48, 220, 39,
            "", names[index], &s_setting_values[index]);
    }

    ui_pixel_action_bar(s_settings_screen, "选择", "修改", "返回");
    refresh_settings();
    lv_screen_load(s_settings_screen);
}

static void build_device_info(void)
{
    clear_settings_screen();
    s_settings_view = SETTINGS_VIEW_DEVICE;
    s_settings_screen = system_device_info_screen_create();
    lv_screen_load(s_settings_screen);
}

void system_settings_enter(void)
{
    build_settings_list(true);
}

void system_settings_exit(void)
{
    clear_settings_screen();
    s_settings_view = SETTINGS_VIEW_LIST;
}

void system_settings_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (s_settings_view == SETTINGS_VIEW_DEVICE) {
        return;
    }
    if (event == BSP_BTN_PRESS && button == BSP_BTN_UP) {
        s_setting_selected = (s_setting_selected + SETTINGS_ROW_COUNT - 1U) %
                             SETTINGS_ROW_COUNT;
        build_settings_list(false);
        return;
    }
    if (event == BSP_BTN_PRESS && button == BSP_BTN_DOWN) {
        s_setting_selected = (s_setting_selected + 1U) % SETTINGS_ROW_COUNT;
        build_settings_list(false);
        return;
    }
    if (event != BSP_BTN_CLICK || button != BSP_BTN_OK) return;
    if (s_setting_selected == SETTINGS_DEVICE_INDEX) {
        build_device_info();
        return;
    }
    if (s_setting_selected == PLUGIN_SETTING_THEME) {
        int32_t theme_index;
        if (ui_theme_select_next(&theme_index) == ESP_OK) build_settings_list(false);
        return;
    }

    int32_t value;
    if (!device_settings_get((uint8_t)s_setting_selected, &value)) return;
    if (s_setting_selected == PLUGIN_SETTING_BRIGHTNESS) {
        value = value >= 100 ? 10 : value + 10;
    } else if (s_setting_selected == PLUGIN_SETTING_VOLUME) {
        value = value >= 100 ? 0 : value + 10;
    } else if (s_setting_selected == PLUGIN_SETTING_KEY_SOUND) {
        value = !value;
    } else if (value == 0) {
        value = 30;
    } else if (value == 30) {
        value = 60;
    } else if (value == 60) {
        value = 180;
    } else if (value == 180) {
        value = 300;
    } else {
        value = 0;
    }
    device_settings_set((uint8_t)s_setting_selected, value);
    build_settings_list(false);
}

bool system_settings_back(void)
{
    if (s_settings_view != SETTINGS_VIEW_DEVICE) return false;
    build_settings_list(false);
    return true;
}
