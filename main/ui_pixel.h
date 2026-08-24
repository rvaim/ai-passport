#pragma once

#include "lvgl.h"
#include "ui_theme.h"

#if !LV_USE_FONT_COMPRESSED
#error "ui_font_zh_14/18 require LV_USE_FONT_COMPRESSED"
#endif

#define UI_SKY        ui_theme_color(PLUGIN_THEME_COLOR_BACKGROUND)
#define UI_INK        ui_theme_color(PLUGIN_THEME_COLOR_TEXT)
#define UI_TEXT_MUTED ui_theme_color(PLUGIN_THEME_COLOR_TEXT_MUTED)
#define UI_PAPER      ui_theme_color(PLUGIN_THEME_COLOR_SURFACE)
#define UI_GRASS      ui_theme_color(PLUGIN_THEME_COLOR_ACCENT)
#define UI_GRASS_DARK ui_theme_color(PLUGIN_THEME_COLOR_ACCENT_STRONG)
#define UI_YELLOW     ui_theme_color(PLUGIN_THEME_COLOR_SELECTION)
#define UI_MUTED      ui_theme_color(PLUGIN_THEME_COLOR_MUTED_SURFACE)
#define UI_DANGER     ui_theme_color(PLUGIN_THEME_COLOR_DANGER)
#define UI_SUCCESS    ui_theme_color(PLUGIN_THEME_COLOR_SUCCESS)

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *message;
    lv_obj_t *options[2];
    lv_obj_t *option_labels[2];
} ui_pixel_dialog_t;

LV_FONT_DECLARE(ui_font_zh_14);
LV_FONT_DECLARE(ui_font_zh_18);

#define UI_FONT_BODY  (&ui_font_zh_14)
#define UI_FONT_TITLE (&ui_font_zh_18)

lv_obj_t *ui_pixel_screen_create(const char *title);
lv_obj_t *ui_pixel_status_bar(lv_obj_t *parent, const char *title);
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color);
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
lv_obj_t *ui_pixel_footer_hint(lv_obj_t *parent, const char *text);
lv_obj_t *ui_pixel_action_bar(lv_obj_t *parent, const char *navigation,
                              const char *ok, const char *back);
lv_obj_t *ui_pixel_row_create(lv_obj_t *parent, int x, int y, int w, int h,
                              const char *icon, const char *label,
                              lv_obj_t **value_label);
lv_obj_t *ui_pixel_empty_state(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *text);
lv_obj_t *ui_pixel_value_card(lv_obj_t *parent, const char *label,
                              const char *value);
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled);
bool ui_pixel_dialog_create(ui_pixel_dialog_t *dialog, lv_obj_t *parent);
void ui_pixel_dialog_show_message(ui_pixel_dialog_t *dialog, const char *title,
                                  const char *message);
void ui_pixel_dialog_show_confirm(ui_pixel_dialog_t *dialog, const char *title,
                                  const char *message, const char *cancel,
                                  const char *confirm, bool confirm_selected);
void ui_pixel_dialog_set_selected(ui_pixel_dialog_t *dialog, bool confirm_selected);
void ui_pixel_dialog_hide(ui_pixel_dialog_t *dialog);
