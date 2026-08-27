#include "passport_ui.h"

#include "bsp_battery.h"
#include "bsp_pins.h"
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
    bool selected;
} passport_ui_list_row_t;

struct passport_ui_list {
    lv_obj_t *root;
    passport_ui_list_row_t *rows;
    size_t capacity;
    size_t count;
    size_t selected;
};

static bool s_battery_available;
static bool s_styles_initialized;
static lv_style_t s_lv_styles[PASSPORT_STYLE_COUNT];

static lv_color_t theme_color(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static lv_text_align_t text_align(uint8_t align)
{
    if (align == PASSPORT_TEXT_ALIGN_CENTER) return LV_TEXT_ALIGN_CENTER;
    if (align == PASSPORT_TEXT_ALIGN_RIGHT) return LV_TEXT_ALIGN_RIGHT;
    return LV_TEXT_ALIGN_LEFT;
}

const lv_font_t *passport_ui_font(void)
{
    return &passport_ui_font_zh_14;
}

static void build_lv_style(passport_style_id_t id)
{
    const passport_style_t *source = passport_theme_style(id);
    lv_style_t *style = &s_lv_styles[id];
    if (s_styles_initialized) lv_style_reset(style);
    else lv_style_init(style);
    if (!source) return;

    lv_style_set_bg_color(style, theme_color(source->background_color));
    lv_style_set_bg_opa(style, source->background_opacity);
    lv_style_set_opa(style, source->opacity);
    lv_style_set_radius(style, source->radius);
    lv_style_set_border_color(style, theme_color(source->border_color));
    lv_style_set_border_width(style, source->border_width);
    lv_style_set_border_opa(style, source->border_opacity);
    lv_style_set_shadow_color(style, theme_color(source->shadow_color));
    lv_style_set_shadow_width(style, source->shadow_width);
    lv_style_set_shadow_spread(style, source->shadow_spread);
    lv_style_set_shadow_opa(style, source->shadow_opacity);
    lv_style_set_shadow_offset_x(style, source->shadow_offset_x);
    lv_style_set_shadow_offset_y(style, source->shadow_offset_y);
    lv_style_set_pad_all(style, source->padding);
    lv_style_set_pad_row(style, source->gap);
    lv_style_set_pad_column(style, source->gap);
    lv_style_set_text_font(style, passport_ui_font());
    lv_style_set_text_color(style, theme_color(source->text_color));
    lv_style_set_text_opa(style, source->text_opacity);
    lv_style_set_text_align(style, text_align(source->text_align));
    lv_style_set_text_line_space(style, source->text_line_spacing);
    lv_style_set_line_color(style, theme_color(source->line_color));
    lv_style_set_line_opa(style, source->line_opacity);
    lv_style_set_line_width(style, source->line_width);
    lv_style_set_arc_color(style, theme_color(source->arc_color));
    lv_style_set_arc_opa(style, source->arc_opacity);
    lv_style_set_arc_width(style, source->arc_width);
}

void passport_ui_theme_refresh(void)
{
    bool report = s_styles_initialized;
    for (size_t i = 0; i < PASSPORT_STYLE_COUNT; ++i) {
        build_lv_style((passport_style_id_t)i);
    }
    s_styles_initialized = true;
    if (report) lv_obj_report_style_change(NULL);
}

void passport_ui_init(bool battery_available)
{
    s_battery_available = battery_available;
    passport_ui_theme_refresh();
}

static bool style_id_valid(passport_style_id_t style)
{
    return (unsigned)style < PASSPORT_STYLE_COUNT;
}

static void apply_style(lv_obj_t *object, passport_style_id_t style)
{
    lv_obj_remove_style_all(object);
    lv_obj_add_style(object, &s_lv_styles[style], LV_PART_MAIN);
}

static void add_style(lv_obj_t *object, passport_style_id_t style,
                      lv_style_selector_t selector)
{
    lv_obj_add_style(object, &s_lv_styles[style], selector);
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

static lv_obj_t *create_text(lv_obj_t *parent, const char *value,
                             passport_style_id_t style)
{
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return NULL;
    apply_style(label, style);
    lv_label_set_text(label, value ? value : "");
    return label;
}

passport_page_t *passport_ui_page_create(const char *title, bool show_status_bar,
                                         bool show_key_bar)
{
    passport_page_t *page = calloc(1, sizeof(*page));
    if (!page) return NULL;
    page->screen = lv_obj_create(NULL);
    apply_style(page->screen, PASSPORT_STYLE_VIEW);
    lv_obj_remove_flag(page->screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page->screen, BSP_LCD_W, BSP_LCD_H);

    page->status = lv_obj_create(page->screen);
    apply_style(page->status, PASSPORT_STYLE_SURFACE);
    lv_obj_remove_flag(page->status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(page->status, 0, 0);
    lv_obj_set_size(page->status, BSP_LCD_W, STATUS_H);
    const passport_style_t *divider = passport_theme_style(PASSPORT_STYLE_DIVIDER);
    lv_obj_set_style_border_width(page->status, divider->border_width, LV_PART_MAIN);
    lv_obj_set_style_border_side(page->status, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(page->status, theme_color(divider->border_color), LV_PART_MAIN);

    page->title = create_text(page->status, title, PASSPORT_STYLE_TEXT);
    lv_obj_set_width(page->title,
                     BSP_LCD_W - (3 * STATUS_SIDE_PAD) - STATUS_BATTERY_W);
    lv_label_set_long_mode(page->title, LV_LABEL_LONG_DOT);
    lv_obj_align(page->title, LV_ALIGN_LEFT_MID, STATUS_SIDE_PAD, 0);

    page->battery = create_text(page->status, "", PASSPORT_STYLE_MUTED_TEXT);
    lv_obj_set_width(page->battery, STATUS_BATTERY_W);
    lv_obj_set_style_text_align(page->battery, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(page->battery, LV_LABEL_LONG_DOT);
    lv_obj_align(page->battery, LV_ALIGN_RIGHT_MID, -STATUS_SIDE_PAD, 0);

    page->content = lv_obj_create(page->screen);
    apply_style(page->content, PASSPORT_STYLE_PAGE);
    lv_obj_remove_flag(page->content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    page->key_bar = lv_obj_create(page->screen);
    apply_style(page->key_bar, PASSPORT_STYLE_SURFACE);
    lv_obj_remove_flag(page->key_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(page->key_bar, 0, BSP_LCD_H - KEY_BAR_H);
    lv_obj_set_size(page->key_bar, BSP_LCD_W, KEY_BAR_H);
    lv_obj_set_style_border_width(page->key_bar, divider->border_width, LV_PART_MAIN);
    lv_obj_set_style_border_side(page->key_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(page->key_bar, theme_color(divider->border_color), LV_PART_MAIN);

    lv_obj_t **keys[3] = {&page->key_nav, &page->key_ok, &page->key_long_ok};
    const passport_style_id_t styles[3] = {
        PASSPORT_STYLE_TEXT, PASSPORT_STYLE_ACCENT_TEXT, PASSPORT_STYLE_MUTED_TEXT,
    };
    for (int i = 0; i < 3; ++i) {
        *keys[i] = create_text(page->key_bar, "", styles[i]);
        lv_obj_set_size(*keys[i], KEY_SLOT_W, passport_ui_font()->line_height);
        lv_obj_set_style_text_align(*keys[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(*keys[i], LV_LABEL_LONG_DOT);
        lv_obj_align(*keys[i], LV_ALIGN_LEFT_MID, i * KEY_SLOT_W, 0);
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

void passport_ui_page_set_action(passport_page_t *page, const char *ok_action)
{
    if (page) set_action_label(page->key_ok, "OK ", ok_action);
}

void passport_ui_page_set_can_go_back(passport_page_t *page, bool can_go_back)
{
    if (page) set_action_label(page->key_long_ok, "长按 ",
                               can_go_back ? "返回" : "主页");
}

lv_obj_t *passport_ui_object_create(passport_page_t *page, lv_obj_t *parent,
                                    passport_ui_object_kind_t kind,
                                    const char *text,
                                    passport_style_id_t style)
{
    if (!page || !style_id_valid(style)) return NULL;
    lv_obj_t *host = parent ? parent : page->content;
    lv_obj_t *object = NULL;
    switch (kind) {
    case PASSPORT_UI_OBJECT_VIEW:
        object = lv_obj_create(host);
        break;
    case PASSPORT_UI_OBJECT_TEXT:
        object = lv_label_create(host);
        break;
    case PASSPORT_UI_OBJECT_BUTTON:
        object = lv_button_create(host);
        break;
    case PASSPORT_UI_OBJECT_IMAGE:
        object = lv_image_create(host);
        break;
    case PASSPORT_UI_OBJECT_LIST:
        object = lv_list_create(host);
        break;
    case PASSPORT_UI_OBJECT_LIST_ITEM:
        if (!parent || !lv_obj_check_type(parent, &lv_list_class)) return NULL;
        object = lv_list_add_button(parent, NULL, text ? text : "");
        break;
    case PASSPORT_UI_OBJECT_BAR:
        object = lv_bar_create(host);
        break;
    case PASSPORT_UI_OBJECT_ARC:
        object = lv_arc_create(host);
        break;
    case PASSPORT_UI_OBJECT_SLIDER:
        object = lv_slider_create(host);
        break;
    case PASSPORT_UI_OBJECT_SWITCH:
        object = lv_switch_create(host);
        break;
    case PASSPORT_UI_OBJECT_SPINNER:
        object = lv_spinner_create(host);
        break;
    case PASSPORT_UI_OBJECT_LINE:
        object = lv_line_create(host);
        break;
    case PASSPORT_UI_OBJECT_CHECKBOX:
        object = lv_checkbox_create(host);
        break;
    case PASSPORT_UI_OBJECT_CANVAS:
        object = lv_canvas_create(host);
        break;
    default:
        return NULL;
    }
    if (!object) return NULL;

    apply_style(object, style);
    switch (kind) {
    case PASSPORT_UI_OBJECT_VIEW:
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
        break;
    case PASSPORT_UI_OBJECT_TEXT:
        lv_label_set_text(object, text ? text : "");
        lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(object, LV_PCT(100));
        break;
    case PASSPORT_UI_OBJECT_BUTTON: {
        add_style(object, PASSPORT_STYLE_BUTTON_PRESSED,
                  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(object, LV_PCT(100), LIST_ROW_H);
        lv_obj_t *label = lv_label_create(object);
        if (!label) {
            lv_obj_delete(object);
            return NULL;
        }
        lv_label_set_text(label, text ? text : "");
        lv_obj_center(label);
        break;
    }
    case PASSPORT_UI_OBJECT_IMAGE:
        break;
    case PASSPORT_UI_OBJECT_LIST:
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_flex_grow(object, 1);
        break;
    case PASSPORT_UI_OBJECT_LIST_ITEM: {
        add_style(object, PASSPORT_STYLE_LIST_ITEM_SELECTED,
                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_width(object, LV_PCT(100));
        break;
    }
    case PASSPORT_UI_OBJECT_BAR:
        add_style(object, PASSPORT_STYLE_INDICATOR, LV_PART_INDICATOR);
        lv_obj_set_size(object, LV_PCT(100), 14);
        break;
    case PASSPORT_UI_OBJECT_ARC:
        add_style(object, PASSPORT_STYLE_INDICATOR, LV_PART_INDICATOR);
        lv_obj_remove_style(object, NULL, LV_PART_KNOB);
        lv_obj_set_size(object, 72, 72);
        break;
    case PASSPORT_UI_OBJECT_SLIDER:
        add_style(object, PASSPORT_STYLE_INDICATOR, LV_PART_INDICATOR);
        add_style(object, PASSPORT_STYLE_KNOB, LV_PART_KNOB);
        lv_obj_set_size(object, LV_PCT(100), 24);
        break;
    case PASSPORT_UI_OBJECT_SWITCH:
        add_style(object, PASSPORT_STYLE_INDICATOR,
                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        add_style(object, PASSPORT_STYLE_KNOB, LV_PART_KNOB);
        lv_obj_set_size(object, 48, 26);
        break;
    case PASSPORT_UI_OBJECT_SPINNER:
        add_style(object, PASSPORT_STYLE_INDICATOR, LV_PART_INDICATOR);
        lv_spinner_set_anim_params(object, 1000, 90);
        lv_obj_set_size(object, 48, 48);
        break;
    case PASSPORT_UI_OBJECT_CHECKBOX:
        add_style(object, PASSPORT_STYLE_KNOB, LV_PART_INDICATOR);
        add_style(object, PASSPORT_STYLE_INDICATOR,
                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_checkbox_set_text(object, text ? text : "");
        lv_obj_set_width(object, LV_PCT(100));
        break;
    case PASSPORT_UI_OBJECT_LINE:
    case PASSPORT_UI_OBJECT_CANVAS:
        break;
    }
    return object;
}

bool passport_ui_object_set_text(lv_obj_t *object,
                                 passport_ui_object_kind_t kind,
                                 const char *text)
{
    if (!object) return false;
    const char *value = text ? text : "";
    if (kind == PASSPORT_UI_OBJECT_TEXT) lv_label_set_text(object, value);
    else if (kind == PASSPORT_UI_OBJECT_CHECKBOX) lv_checkbox_set_text(object, value);
    else if (kind == PASSPORT_UI_OBJECT_BUTTON ||
             kind == PASSPORT_UI_OBJECT_LIST_ITEM) {
        lv_obj_t *label = lv_obj_get_child(object, 0);
        if (!label) return false;
        lv_label_set_text(label, value);
    } else return false;
    return true;
}

bool passport_ui_object_set_value(lv_obj_t *object,
                                  passport_ui_object_kind_t kind,
                                  int32_t value, bool animate)
{
    if (!object) return false;
    lv_anim_enable_t animation = animate ? LV_ANIM_ON : LV_ANIM_OFF;
    if (kind == PASSPORT_UI_OBJECT_BAR) lv_bar_set_value(object, value, animation);
    else if (kind == PASSPORT_UI_OBJECT_ARC) lv_arc_set_value(object, value);
    else if (kind == PASSPORT_UI_OBJECT_SLIDER) {
        lv_slider_set_value(object, value, animation);
    } else return false;
    return true;
}

bool passport_ui_object_set_range(lv_obj_t *object,
                                  passport_ui_object_kind_t kind,
                                  int32_t minimum, int32_t maximum)
{
    if (!object || minimum >= maximum) return false;
    if (kind == PASSPORT_UI_OBJECT_BAR) lv_bar_set_range(object, minimum, maximum);
    else if (kind == PASSPORT_UI_OBJECT_ARC) lv_arc_set_range(object, minimum, maximum);
    else if (kind == PASSPORT_UI_OBJECT_SLIDER) {
        lv_slider_set_range(object, minimum, maximum);
    } else return false;
    return true;
}

bool passport_ui_object_set_checked(lv_obj_t *object,
                                    passport_ui_object_kind_t kind,
                                    bool checked)
{
    if (!object || (kind != PASSPORT_UI_OBJECT_SWITCH &&
                    kind != PASSPORT_UI_OBJECT_CHECKBOX)) return false;
    if (checked) lv_obj_add_state(object, LV_STATE_CHECKED);
    else lv_obj_remove_state(object, LV_STATE_CHECKED);
    return true;
}

bool passport_ui_object_set_selected(lv_obj_t *object,
                                     passport_ui_object_kind_t kind,
                                     bool selected)
{
    if (!object || kind != PASSPORT_UI_OBJECT_LIST_ITEM) return false;
    if (selected) {
        lv_obj_add_state(object, LV_STATE_CHECKED);
        lv_obj_scroll_to_view(object, LV_ANIM_OFF);
    } else {
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    }
    return true;
}

bool passport_ui_object_set_pressed(lv_obj_t *object,
                                    passport_ui_object_kind_t kind,
                                    bool pressed)
{
    if (!object || kind != PASSPORT_UI_OBJECT_BUTTON) return false;
    if (pressed) lv_obj_add_state(object, LV_STATE_PRESSED);
    else lv_obj_remove_state(object, LV_STATE_PRESSED);
    return true;
}

bool passport_ui_object_replace_style(lv_obj_t *object,
                                      passport_style_id_t old_style,
                                      passport_style_id_t new_style)
{
    if (!object || !style_id_valid(old_style) || !style_id_valid(new_style)) return false;
    return lv_obj_replace_style(object, &s_lv_styles[old_style],
                                &s_lv_styles[new_style], LV_PART_MAIN);
}

bool passport_ui_object_set_property(lv_obj_t *object,
                                     passport_style_property_t property,
                                     int32_t value)
{
    if (!object) return false;
    switch (property) {
    case PASSPORT_STYLE_PROP_BACKGROUND_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_bg_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_BACKGROUND_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_bg_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_RADIUS:
        if (value < 0 || value > 32) return false;
        lv_obj_set_style_radius(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_BORDER_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_border_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_BORDER_WIDTH:
        if (value < 0 || value > 4) return false;
        lv_obj_set_style_border_width(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_BORDER_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_border_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_shadow_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_WIDTH:
        if (value < 0 || value > 12) return false;
        lv_obj_set_style_shadow_width(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_SPREAD:
        if (value < 0 || value > 6) return false;
        lv_obj_set_style_shadow_spread(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_shadow_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_OFFSET_X:
        if (value < -8 || value > 8) return false;
        lv_obj_set_style_shadow_offset_x(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y:
        if (value < -8 || value > 8) return false;
        lv_obj_set_style_shadow_offset_y(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_PADDING:
        if (value < 0 || value > 24) return false;
        lv_obj_set_style_pad_all(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_GAP:
        if (value < 0 || value > 24) return false;
        lv_obj_set_style_pad_row(object, value, LV_PART_MAIN);
        lv_obj_set_style_pad_column(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_TEXT_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_text_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_TEXT_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_text_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_TEXT_ALIGN:
        if (value < PASSPORT_TEXT_ALIGN_LEFT || value > PASSPORT_TEXT_ALIGN_RIGHT) return false;
        lv_obj_set_style_text_align(object, text_align((uint8_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_TEXT_LINE_SPACING:
        if (value < -8 || value > 16) return false;
        lv_obj_set_style_text_line_space(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_LINE_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_line_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_LINE_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_line_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_LINE_WIDTH:
        if (value < 0 || value > 8) return false;
        lv_obj_set_style_line_width(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_ARC_COLOR:
        if ((uint32_t)value > 0xFFFFFFU) return false;
        lv_obj_set_style_arc_color(object, theme_color((uint32_t)value), LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_ARC_OPACITY:
        if (value < 0 || value > 255) return false;
        lv_obj_set_style_arc_opa(object, value, LV_PART_MAIN);
        break;
    case PASSPORT_STYLE_PROP_ARC_WIDTH:
        if (value < 0 || value > 16) return false;
        lv_obj_set_style_arc_width(object, value, LV_PART_MAIN);
        break;
    default:
        return false;
    }
    return true;
}

lv_obj_t *passport_ui_label_create(passport_page_t *page, const char *text)
{
    lv_obj_t *label = passport_ui_object_create(
        page, NULL, PASSPORT_UI_OBJECT_TEXT, text, PASSPORT_STYLE_TEXT);
    if (!label) return NULL;
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}

void passport_ui_label_set_text(lv_obj_t *label, const char *text)
{
    if (label) lv_label_set_text(label, text ? text : "");
}

static void list_refresh(passport_ui_list_t *list)
{
    const passport_style_t *normal = passport_theme_style(PASSPORT_STYLE_LIST_ITEM);
    const passport_style_t *selected_style = passport_theme_style(
        PASSPORT_STYLE_LIST_ITEM_SELECTED);
    const passport_style_t *accent = passport_theme_style(PASSPORT_STYLE_ACCENT_TEXT);
    for (size_t i = 0; i < list->count; ++i) {
        passport_ui_list_row_t *row = &list->rows[i];
        bool selected = i == list->selected;
        if (selected != row->selected) {
            passport_ui_object_replace_style(
                row->root,
                row->selected ? PASSPORT_STYLE_LIST_ITEM_SELECTED : PASSPORT_STYLE_LIST_ITEM,
                selected ? PASSPORT_STYLE_LIST_ITEM_SELECTED : PASSPORT_STYLE_LIST_ITEM);
            row->selected = selected;
        }
        lv_obj_set_style_text_color(row->label,
                                    theme_color(selected ? selected_style->text_color :
                                                normal->text_color), LV_PART_MAIN);
        if (row->value) {
            lv_obj_set_style_text_color(row->value,
                                        theme_color(selected ? selected_style->text_color :
                                                    accent->text_color), LV_PART_MAIN);
        }
    }
}

passport_ui_list_t *passport_ui_list_create(passport_page_t *page, size_t capacity)
{
    if (!page || capacity == 0U || capacity > 32U) return NULL;
    passport_ui_list_t *list = calloc(1, sizeof(*list));
    if (!list) return NULL;
    list->rows = calloc(capacity, sizeof(*list->rows));
    if (!list->rows) {
        free(list);
        return NULL;
    }
    list->capacity = capacity;
    list->root = passport_ui_object_create(
        page, NULL, PASSPORT_UI_OBJECT_VIEW, NULL, PASSPORT_STYLE_PAGE);
    if (!list->root) {
        free(list->rows);
        free(list);
        return NULL;
    }
    lv_obj_set_width(list->root, LV_PCT(100));
    lv_obj_set_flex_grow(list->root, 1);
    lv_obj_set_flex_flow(list->root, LV_FLEX_FLOW_COLUMN);

    const passport_style_t *item = passport_theme_style(PASSPORT_STYLE_LIST_ITEM);
    int shadow_blur = item->shadow_width + item->shadow_spread;
    bool shadow_visible = item->shadow_width > 0U && item->shadow_opacity > 0U;
    int left = shadow_visible ? shadow_blur - item->shadow_offset_x : 0;
    int right = shadow_visible ? shadow_blur + item->shadow_offset_x : 0;
    int top = shadow_visible ? shadow_blur - item->shadow_offset_y : 0;
    int bottom = shadow_visible ? shadow_blur + item->shadow_offset_y : 0;
    lv_obj_set_style_pad_left(list->root, left > 0 ? left : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(list->root, right > 0 ? right : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(list->root, top > 0 ? top : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list->root, bottom > 0 ? bottom : 0, LV_PART_MAIN);
    int row_gap = item->gap / 2;
    int shadow_gap = shadow_visible ? item->shadow_spread + abs(item->shadow_offset_y) : 0;
    if (row_gap < 2) row_gap = 2;
    if (row_gap < shadow_gap) row_gap = shadow_gap;
    lv_obj_set_style_pad_row(list->root, row_gap, LV_PART_MAIN);
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
    bool selected = list->count == list->selected;
    passport_style_id_t style = selected ? PASSPORT_STYLE_LIST_ITEM_SELECTED :
                                           PASSPORT_STYLE_LIST_ITEM;
    lv_obj_t *row = create_text(list->root, text, style);
    if (!row) return false;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LIST_ROW_H);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    list->rows[list->count++] = (passport_ui_list_row_t) {
        .root = row,
        .label = row,
        .selected = selected,
    };
    return true;
}

bool passport_ui_list_add_value(passport_ui_list_t *list,
                                const char *text,
                                const char *value)
{
    if (!list || list->count >= list->capacity) return false;
    bool selected = list->count == list->selected;
    passport_style_id_t row_style = selected ? PASSPORT_STYLE_LIST_ITEM_SELECTED :
                                               PASSPORT_STYLE_LIST_ITEM;
    lv_obj_t *row = lv_obj_create(list->root);
    if (!row) return false;
    apply_style(row, row_style);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LIST_ROW_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = create_text(row, text, PASSPORT_STYLE_TEXT);
    lv_obj_set_height(label, passport_ui_font()->line_height);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    lv_obj_t *value_label = create_text(row, value, PASSPORT_STYLE_ACCENT_TEXT);
    lv_obj_set_size(value_label, 76, passport_ui_font()->line_height);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);

    list->rows[list->count++] = (passport_ui_list_row_t) {
        .root = row,
        .label = label,
        .value = value_label,
        .selected = selected,
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
    if (!list || list->count == 0U || delta == 0) return;
    int count = (int)list->count;
    int next = ((int)list->selected + delta) % count;
    if (next < 0) next += count;
    list->selected = (size_t)next;
    list_refresh(list);
}

size_t passport_ui_list_selected(const passport_ui_list_t *list)
{
    return list ? list->selected : 0U;
}
