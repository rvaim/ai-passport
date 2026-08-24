#include "ui_pixel.h"

#include "bsp_battery.h"

#include "esp_timer.h"

#include <stdio.h>

static lv_obj_t *s_status_owner;
static lv_obj_t *s_battery_label;
static lv_timer_t *s_battery_timer;
static int s_cached_battery = -1;
static int64_t s_battery_read_at;

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static const char *battery_symbol(int level)
{
    if (level < 0) return LV_SYMBOL_BATTERY_EMPTY;
    if (level > 80) return LV_SYMBOL_BATTERY_FULL;
    if (level > 60) return LV_SYMBOL_BATTERY_3;
    if (level > 35) return LV_SYMBOL_BATTERY_2;
    if (level > 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static int cached_battery_level(void)
{
    int64_t now = esp_timer_get_time();
    if (s_battery_read_at == 0 || now - s_battery_read_at >= 30000000LL) {
        s_cached_battery = bsp_battery_soc();
        s_battery_read_at = now;
    }
    return s_cached_battery;
}

static void set_battery_text(lv_obj_t *label)
{
    char text[24];
    int level = cached_battery_level();
    if (!label) return;
    if (level < 0) snprintf(text, sizeof(text), "%s --%%", battery_symbol(level));
    else snprintf(text, sizeof(text), "%s %d%%", battery_symbol(level), level);
    lv_label_set_text(label, text);
}

static void refresh_battery(lv_timer_t *timer)
{
    (void)timer;
    set_battery_text(s_battery_label);
}

static void status_owner_deleted(lv_event_t *event)
{
    if (lv_event_get_target_obj(event) != s_status_owner) return;
    if (s_battery_timer) lv_timer_delete(s_battery_timer);
    s_battery_timer = NULL;
    s_battery_label = NULL;
    s_status_owner = NULL;
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    const plugin_theme_descriptor_t *theme = ui_theme_descriptor();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    block(scr, 0, 294, 240, 26, UI_GRASS);
    if (theme->decoration == PLUGIN_THEME_DECORATION_PIXEL_GROUND) {
        block(scr, 0, 294, 240, 3, UI_SUCCESS);
        for (int x = 0; x < 240; x += 30) {
            block(scr, x, 312, 18, 8, UI_GRASS_DARK);
            block(scr, x + 18, 316, 12, 4, 0x75452E);
        }
    }

    s_battery_label = ui_pixel_status_bar(scr, title);

    s_status_owner = scr;
    lv_obj_add_event_cb(scr, status_owner_deleted, LV_EVENT_DELETE, NULL);
    s_battery_timer = lv_timer_create(refresh_battery, 30000U, NULL);
    return scr;
}

lv_obj_t *ui_pixel_status_bar(lv_obj_t *parent, const char *title)
{
    lv_obj_t *status = block(parent, 0, 0, 240, 30, UI_PAPER);
    block(parent, 0, 27, 240, 3, UI_INK);
    lv_obj_t *heading = ui_pixel_label(status, title, UI_FONT_BODY, UI_INK);
    lv_obj_align(heading, LV_ALIGN_LEFT_MID, 8, -1);
    lv_obj_t *battery = ui_pixel_label(status, "", UI_FONT_BODY, UI_INK);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -8, -1);
    set_battery_text(battery);
    return battery;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    const plugin_theme_descriptor_t *theme = ui_theme_descriptor();
    lv_obj_t *panel = block(parent, x, y, w, h, color);
    /* Keep the shadow on the panel so hide/delete cannot leave an orphan sibling. */
    lv_obj_set_style_radius(panel, theme->panel_radius, 0);
    lv_obj_set_style_shadow_color(
        panel, lv_color_hex(ui_theme_color(PLUGIN_THEME_COLOR_BORDER)), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(panel, theme->panel_shadow_width, 0);
    lv_obj_set_style_shadow_offset_x(panel, theme->panel_shadow_offset_x, 0);
    lv_obj_set_style_shadow_offset_y(panel, theme->panel_shadow_offset_y, 0);
    lv_obj_set_style_shadow_spread(panel, 0, 0);
    lv_obj_set_style_border_color(
        panel, lv_color_hex(ui_theme_color(PLUGIN_THEME_COLOR_BORDER)), 0);
    lv_obj_set_style_border_width(panel, theme->panel_border_width, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    return panel;
}

lv_obj_t *ui_pixel_footer_hint(lv_obj_t *parent, const char *text)
{
    lv_obj_t *hint = ui_pixel_label(parent, text, UI_FONT_BODY, UI_INK);
    lv_obj_set_width(hint, 232);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(hint, 4, 299);
    return hint;
}

static lv_obj_t *action_label(lv_obj_t *parent, int x, const char *prefix,
                              const char *text)
{
    char value[48];
    if (!text || text[0] == '\0') value[0] = '\0';
    else snprintf(value, sizeof(value), "%s%s", prefix, text);
    lv_obj_t *label = ui_pixel_label(parent, value, UI_FONT_BODY, UI_INK);
    lv_obj_set_pos(label, x, 3);
    lv_obj_set_size(label, 80, 20);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

lv_obj_t *ui_pixel_action_bar(lv_obj_t *parent, const char *navigation,
                              const char *ok, const char *back)
{
    lv_obj_t *bar = block(parent, 0, 294, 240, 26, UI_GRASS);
    action_label(bar, 0, LV_SYMBOL_UP LV_SYMBOL_DOWN " ", navigation);
    action_label(bar, 80, "OK ", ok);
    action_label(bar, 160, "长按 ", back);
    return bar;
}

lv_obj_t *ui_pixel_row_create(lv_obj_t *parent, int x, int y, int w, int h,
                              const char *icon, const char *label,
                              lv_obj_t **value_label)
{
    lv_obj_t *panel = ui_pixel_panel_create(parent, x, y, w, h, UI_PAPER);
    char heading[96];
    if (icon && icon[0] != '\0') snprintf(heading, sizeof(heading), "%s  %s", icon, label);
    else snprintf(heading, sizeof(heading), "%s", label);
    lv_obj_t *name = ui_pixel_label(panel, heading, UI_FONT_BODY, UI_INK);
    lv_obj_set_width(name, w - 72);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_t *value = ui_pixel_label(panel, "", UI_FONT_BODY, UI_INK);
    lv_obj_set_width(value, 72);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -2, 0);
    if (value_label) *value_label = value;
    return panel;
}

lv_obj_t *ui_pixel_empty_state(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *text)
{
    lv_obj_t *panel = ui_pixel_panel_create(parent, x, y, w, h, UI_MUTED);
    lv_obj_t *label = ui_pixel_label(panel, text, UI_FONT_BODY, UI_INK);
    lv_obj_set_width(label, w - 20);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return panel;
}

lv_obj_t *ui_pixel_value_card(lv_obj_t *parent, const char *label,
                              const char *value)
{
    lv_obj_t *panel = ui_pixel_panel_create(parent, 22, 74, 196, 128, UI_PAPER);
    lv_obj_t *name = ui_pixel_label(panel, label, UI_FONT_BODY, UI_INK);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *content = ui_pixel_label(panel, value, UI_FONT_BODY, UI_INK);
    lv_obj_set_width(content, 170);
    lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 17);
    return panel;
}

bool ui_pixel_dialog_create(ui_pixel_dialog_t *dialog, lv_obj_t *parent)
{
    if (!dialog || !parent) return false;
    *dialog = (ui_pixel_dialog_t) {0};
    dialog->overlay = lv_obj_create(parent);
    if (!dialog->overlay) return false;
    lv_obj_remove_flag(dialog->overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dialog->overlay, 0, 30);
    lv_obj_set_size(dialog->overlay, 240, 264);
    lv_obj_set_style_border_width(dialog->overlay, 0, 0);
    lv_obj_set_style_pad_all(dialog->overlay, 0, 0);
    lv_obj_set_style_bg_color(dialog->overlay, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(dialog->overlay, LV_OPA_50, 0);

    dialog->panel = ui_pixel_panel_create(dialog->overlay, 18, 44, 204, 164, UI_PAPER);
    dialog->title = ui_pixel_label(dialog->panel, "", UI_FONT_TITLE, UI_INK);
    lv_obj_set_width(dialog->title, 176);
    lv_obj_set_style_text_align(dialog->title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dialog->title, LV_ALIGN_TOP_MID, 0, 4);
    dialog->message = ui_pixel_label(dialog->panel, "", UI_FONT_BODY, UI_INK);
    lv_obj_set_width(dialog->message, 176);
    lv_obj_set_style_text_align(dialog->message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(dialog->message, LV_LABEL_LONG_WRAP);
    lv_obj_align(dialog->message, LV_ALIGN_TOP_MID, 0, 38);
    for (size_t index = 0; index < 2U; ++index) {
        dialog->options[index] = ui_pixel_panel_create(
            dialog->panel, 12, 88 + (int)index * 34, 166, 27, UI_PAPER);
        dialog->option_labels[index] = ui_pixel_label(
            dialog->options[index], "", UI_FONT_BODY, UI_INK);
        lv_obj_center(dialog->option_labels[index]);
    }
    ui_pixel_dialog_hide(dialog);
    return true;
}

void ui_pixel_dialog_show_message(ui_pixel_dialog_t *dialog, const char *title,
                                  const char *message)
{
    if (!dialog || !dialog->overlay) return;
    lv_label_set_text(dialog->title, title);
    lv_label_set_text(dialog->message, message);
    lv_obj_align(dialog->message, LV_ALIGN_CENTER, 0, 15);
    for (size_t index = 0; index < 2U; ++index) {
        lv_obj_add_flag(dialog->options[index], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(dialog->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(dialog->overlay);
}

void ui_pixel_dialog_show_confirm(ui_pixel_dialog_t *dialog, const char *title,
                                  const char *message, const char *cancel,
                                  const char *confirm, bool confirm_selected)
{
    if (!dialog || !dialog->overlay) return;
    lv_label_set_text(dialog->title, title);
    lv_label_set_text(dialog->message, message);
    lv_obj_align(dialog->message, LV_ALIGN_TOP_MID, 0, 38);
    lv_label_set_text(dialog->option_labels[0], cancel);
    lv_label_set_text(dialog->option_labels[1], confirm);
    for (size_t index = 0; index < 2U; ++index) {
        lv_obj_remove_flag(dialog->options[index], LV_OBJ_FLAG_HIDDEN);
    }
    ui_pixel_dialog_set_selected(dialog, confirm_selected);
    lv_obj_remove_flag(dialog->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(dialog->overlay);
}

void ui_pixel_dialog_set_selected(ui_pixel_dialog_t *dialog, bool confirm_selected)
{
    if (!dialog || !dialog->overlay) return;
    ui_pixel_set_selected(dialog->options[0], !confirm_selected, true);
    ui_pixel_set_selected(dialog->options[1], confirm_selected, true);
}

void ui_pixel_dialog_hide(ui_pixel_dialog_t *dialog)
{
    if (!dialog || !dialog->overlay) return;
    lv_obj_add_flag(dialog->overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    uint32_t color = !enabled ? UI_MUTED : (selected ? UI_YELLOW : UI_PAPER);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(panel,
        lv_color_hex(selected
            ? ui_theme_color(PLUGIN_THEME_COLOR_SELECTION_BORDER)
            : ui_theme_color(PLUGIN_THEME_COLOR_BORDER)), 0);
}
