#include "passport_ui.h"

#include "bsp_battery.h"
#include "bsp_pins.h"
#include "passport_theme.h"
#include <stdlib.h>

#define STATUS_H 28
#define KEY_BAR_H 30
#define LIST_ROW_H 34
#define KEY_SLOT_W (BSP_LCD_W / 3)
#define STATUS_SIDE_PAD 6
#define STATUS_BATTERY_W 72

LV_FONT_DECLARE(passport_ui_font_zh_14);

struct passport_page {
    lv_obj_t *screen;
    lv_obj_t *status;
    lv_obj_t *title;
    lv_obj_t *battery;
    lv_obj_t *content;
    lv_obj_t *key_bar;
    lv_obj_t *key_nav;
    lv_obj_t *key_ok;
    lv_obj_t *key_long_ok;
    lv_timer_t *status_timer;
    bool status_visible;
    bool key_bar_visible;
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *label;
    lv_obj_t *value;
} passport_ui_list_row_t;

struct passport_ui_list {
    lv_obj_t *root;
    passport_ui_list_row_t *rows;
    size_t capacity;
    size_t count;
    size_t selected;
};

static bool s_battery_available;

static lv_color_t theme_color(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void style_label(lv_obj_t *label, uint32_t color)
{
    lv_obj_set_style_text_font(label, passport_ui_font(), 0);
    lv_obj_set_style_text_color(label, theme_color(color), 0);
}

const lv_font_t *passport_ui_font(void)
{
    return &passport_ui_font_zh_14;
}

void passport_ui_init(bool battery_available)
{
    s_battery_available = battery_available;
}

static void style_plain(lv_obj_t *obj, uint32_t bg)
{
    const passport_theme_tokens_t *t = passport_theme_current();
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, theme_color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_text_font(obj, passport_ui_font(), 0);
    lv_obj_set_style_text_color(obj, theme_color(t->text), 0);
}

static void style_list_item(lv_obj_t *item)
{
    const passport_theme_tokens_t *t = passport_theme_current();
    lv_obj_set_style_radius(item, t->radius, 0);
    lv_obj_set_style_border_width(item, t->border_width, 0);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_border_color(item, theme_color(t->border), 0);
    lv_obj_set_style_shadow_color(item, theme_color(t->shadow), 0);
    lv_obj_set_style_shadow_width(item, t->shadow_width, 0);
    lv_obj_set_style_shadow_spread(item, t->shadow_spread, 0);
    lv_obj_set_style_shadow_offset_x(item, t->shadow_offset_x, 0);
    lv_obj_set_style_shadow_offset_y(item, t->shadow_offset_y, 0);
    lv_obj_set_style_shadow_opa(item, t->shadow_opacity, 0);
}

static void update_status(lv_timer_t *timer)
{
    passport_page_t *page = (passport_page_t *)lv_timer_get_user_data(timer);
    if (!page || !page->battery || !page->status_visible) return;
    if (!s_battery_available) {
        lv_label_set_text(page->battery, "电量 --");
        return;
    }
    int soc = bsp_battery_soc();
    if (soc < 0) lv_label_set_text(page->battery, "电量 --");
    else lv_label_set_text_fmt(page->battery, "电量 %d%%", soc);
}

static void update_layout(passport_page_t *page)
{
    int top = page->status_visible ? STATUS_H : 0;
    int bottom = page->key_bar_visible ? KEY_BAR_H : 0;
    if (page->status_visible) lv_obj_remove_flag(page->status, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page->status, LV_OBJ_FLAG_HIDDEN);
    if (page->key_bar_visible) lv_obj_remove_flag(page->key_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page->key_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(page->content, 0, top);
    lv_obj_set_size(page->content, BSP_LCD_W, BSP_LCD_H - top - bottom);
}

passport_page_t *passport_ui_page_create(const char *title, bool show_status_bar, bool show_key_bar)
{
    passport_page_t *page = calloc(1, sizeof(*page));
    if (!page) return NULL;
    const passport_theme_tokens_t *t = passport_theme_current();
    page->screen = lv_obj_create(NULL);
    style_plain(page->screen, t->background);
    lv_obj_set_size(page->screen, BSP_LCD_W, BSP_LCD_H);

    page->status = lv_obj_create(page->screen);
    style_plain(page->status, t->surface);
    lv_obj_set_pos(page->status, 0, 0);
    lv_obj_set_size(page->status, BSP_LCD_W, STATUS_H);
    lv_obj_set_style_border_width(page->status, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(page->status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(page->status, theme_color(t->divider), 0);

    page->title = lv_label_create(page->status);
    style_label(page->title, t->text);
    lv_obj_set_width(page->title,
                     BSP_LCD_W - (3 * STATUS_SIDE_PAD) - STATUS_BATTERY_W);
    lv_label_set_long_mode(page->title, LV_LABEL_LONG_DOT);
    lv_obj_align(page->title, LV_ALIGN_LEFT_MID, STATUS_SIDE_PAD, 0);
    lv_label_set_text(page->title, title ? title : "");

    page->battery = lv_label_create(page->status);
    style_label(page->battery, t->muted_text);
    lv_obj_set_width(page->battery, STATUS_BATTERY_W);
    lv_obj_set_style_text_align(page->battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(page->battery, LV_LABEL_LONG_DOT);
    lv_obj_align(page->battery, LV_ALIGN_RIGHT_MID, -STATUS_SIDE_PAD, 0);

    page->content = lv_obj_create(page->screen);
    style_plain(page->content, t->background);
    lv_obj_set_style_pad_all(page->content, t->spacing, 0);
    lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    page->key_bar = lv_obj_create(page->screen);
    style_plain(page->key_bar, t->surface);
    lv_obj_set_pos(page->key_bar, 0, BSP_LCD_H - KEY_BAR_H);
    lv_obj_set_size(page->key_bar, BSP_LCD_W, KEY_BAR_H);
    lv_obj_set_style_border_width(page->key_bar, 1, 0);
    lv_obj_set_style_border_side(page->key_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(page->key_bar, theme_color(t->divider), 0);

    lv_obj_t **keys[3] = {&page->key_nav, &page->key_ok, &page->key_long_ok};
    uint32_t colors[3] = {t->text, t->accent, t->muted_text};
    for (int i = 0; i < 3; ++i) {
        *keys[i] = lv_label_create(page->key_bar);
        lv_obj_set_size(*keys[i], KEY_SLOT_W, passport_ui_font()->line_height);
        style_label(*keys[i], colors[i]);
        lv_obj_set_style_text_align(*keys[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(*keys[i], LV_LABEL_LONG_DOT);
        lv_obj_align(*keys[i], LV_ALIGN_LEFT_MID, i * KEY_SLOT_W, 0);
        lv_label_set_text(*keys[i], "");
    }
    lv_label_set_text(page->key_nav, LV_SYMBOL_UP LV_SYMBOL_DOWN " (选择)");

    page->status_visible = show_status_bar;
    page->key_bar_visible = show_key_bar;
    update_layout(page);
    page->status_timer = lv_timer_create(update_status, 5000, page);
    update_status(page->status_timer);
    return page;
}

void passport_ui_page_destroy(passport_page_t *page)
{
    if (!page) return;
    if (page->status_timer) lv_timer_delete(page->status_timer);
    if (page->screen) lv_obj_delete(page->screen);
    free(page);
}

void passport_ui_page_show(passport_page_t *page)
{
    if (page && page->screen) lv_screen_load(page->screen);
}

void passport_ui_page_set_status_bar(passport_page_t *page, bool visible)
{
    if (!page) return;
    page->status_visible = visible;
    update_layout(page);
}

void passport_ui_page_set_key_bar(passport_page_t *page, bool visible)
{
    if (!page) return;
    page->key_bar_visible = visible;
    update_layout(page);
}

static void set_action_label(lv_obj_t *label, const char *prefix, const char *action)
{
    if (!action || action[0] == '\0') {
        lv_label_set_text(label, "");
        return;
    }
    lv_label_set_text_fmt(label, "%s%s", prefix, action);
}

void passport_ui_page_set_actions(passport_page_t *page,
                                  const char *ok_action,
                                  const char *long_ok_action)
{
    if (!page) return;
    set_action_label(page->key_ok, "OK ", ok_action);
    set_action_label(page->key_long_ok, "长按 ", long_ok_action);
}

lv_obj_t *passport_ui_label_create(passport_page_t *page, const char *text)
{
    if (!page) return NULL;
    const passport_theme_tokens_t *t = passport_theme_current();
    lv_obj_t *label = lv_label_create(page->content);
    style_label(label, t->text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 3, 0);
    lv_label_set_text(label, text ? text : "");
    return label;
}

void passport_ui_label_set_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text ? text : "");
}

static void list_refresh(passport_ui_list_t *list)
{
    const passport_theme_tokens_t *t = passport_theme_current();
    for (size_t i = 0; i < list->count; ++i) {
        passport_ui_list_row_t *row = &list->rows[i];
        const bool selected = i == list->selected;
        lv_obj_set_style_bg_color(row->root,
                                  theme_color(selected ? t->accent : t->item_background), 0);
        lv_obj_set_style_bg_opa(row->root, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(row->label,
                                    theme_color(selected ? t->selection_text : t->text), 0);
        if (row->value) {
            lv_obj_set_style_text_color(row->value,
                                        theme_color(selected ? t->selection_text : t->accent), 0);
        }
    }
}

passport_ui_list_t *passport_ui_list_create(passport_page_t *page, size_t capacity)
{
    if (!page || capacity == 0 || capacity > 32) return NULL;
    passport_ui_list_t *list = calloc(1, sizeof(*list));
    if (!list) return NULL;
    list->rows = calloc(capacity, sizeof(*list->rows));
    if (!list->rows) {
        free(list);
        return NULL;
    }
    list->capacity = capacity;
    list->root = lv_obj_create(page->content);
    const passport_theme_tokens_t *t = passport_theme_current();
    style_plain(list->root, t->background);
    lv_obj_set_width(list->root, LV_PCT(100));
    lv_obj_set_flex_grow(list->root, 1);
    lv_obj_set_flex_flow(list->root, LV_FLEX_FLOW_COLUMN);
    int shadow_blur = t->shadow_width + t->shadow_spread;
    bool shadow_visible = t->shadow_width > 0 && t->shadow_opacity > 0;
    int left = shadow_visible ? shadow_blur - t->shadow_offset_x : 0;
    int right = shadow_visible ? shadow_blur + t->shadow_offset_x : 0;
    int top = shadow_visible ? shadow_blur - t->shadow_offset_y : 0;
    int bottom = shadow_visible ? shadow_blur + t->shadow_offset_y : 0;
    lv_obj_set_style_pad_left(list->root, left > 0 ? left : 0, 0);
    lv_obj_set_style_pad_right(list->root, right > 0 ? right : 0, 0);
    lv_obj_set_style_pad_top(list->root, top > 0 ? top : 0, 0);
    lv_obj_set_style_pad_bottom(list->root, bottom > 0 ? bottom : 0, 0);
    int row_gap = t->spacing / 2;
    int shadow_gap = shadow_visible ? t->shadow_spread + abs(t->shadow_offset_y) : 0;
    if (row_gap < 2) row_gap = 2;
    if (row_gap < shadow_gap) row_gap = shadow_gap;
    lv_obj_set_style_pad_row(list->root, row_gap, 0);
    return list;
}

void passport_ui_list_destroy(passport_ui_list_t *list)
{
    if (!list) return;
    if (list->root) lv_obj_delete(list->root);
    free(list->rows);
    free(list);
}

bool passport_ui_list_add(passport_ui_list_t *list, const char *text)
{
    if (!list || list->count >= list->capacity) return false;
    const passport_theme_tokens_t *t = passport_theme_current();
    lv_obj_t *row = lv_label_create(list->root);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LIST_ROW_H);
    bool selected = list->count == list->selected;
    style_label(row, selected ? t->selection_text : t->text);
    lv_obj_set_style_bg_color(row,
                              theme_color(selected ? t->accent : t->item_background), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    style_list_item(row);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    lv_label_set_text(row, text ? text : "");
    list->rows[list->count++] = (passport_ui_list_row_t) {
        .root = row,
        .label = row,
        .value = NULL,
    };
    return true;
}

bool passport_ui_list_add_value(passport_ui_list_t *list,
                                const char *text,
                                const char *value)
{
    if (!list || list->count >= list->capacity) return false;
    const passport_theme_tokens_t *t = passport_theme_current();
    lv_obj_t *row = lv_obj_create(list->root);
    style_plain(row, t->item_background);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LIST_ROW_H);
    lv_obj_set_style_bg_color(row, theme_color(t->accent), 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    style_list_item(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    style_label(label, t->text);
    lv_obj_set_height(label, passport_ui_font()->line_height);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text ? text : "");

    lv_obj_t *value_label = lv_label_create(row);
    style_label(value_label, t->accent);
    lv_obj_set_size(value_label, 76, passport_ui_font()->line_height);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(value_label, value ? value : "");

    list->rows[list->count++] = (passport_ui_list_row_t) {
        .root = row,
        .label = label,
        .value = value_label,
    };
    list_refresh(list);
    return true;
}

bool passport_ui_list_set_value(passport_ui_list_t *list,
                                size_t index,
                                const char *value)
{
    if (!list || index >= list->count || !list->rows[index].value) return false;
    lv_label_set_text(list->rows[index].value, value ? value : "");
    return true;
}

void passport_ui_list_move(passport_ui_list_t *list, int delta)
{
    if (!list || list->count == 0 || delta == 0) return;
    int n = (int)list->count;
    int next = ((int)list->selected + delta) % n;
    if (next < 0) next += n;
    list->selected = (size_t)next;
    list_refresh(list);
}

size_t passport_ui_list_selected(const passport_ui_list_t *list)
{
    return list ? list->selected : 0;
}
