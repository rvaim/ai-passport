#pragma once

#include "lvgl.h"
#include "passport_theme.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct passport_page passport_page_t;
typedef struct passport_ui_list passport_ui_list_t;

typedef enum {
    PASSPORT_UI_OBJECT_VIEW = 0,
    PASSPORT_UI_OBJECT_TEXT,
    PASSPORT_UI_OBJECT_BUTTON,
    PASSPORT_UI_OBJECT_IMAGE,
    PASSPORT_UI_OBJECT_LIST,
    PASSPORT_UI_OBJECT_LIST_ITEM,
    PASSPORT_UI_OBJECT_BAR,
    PASSPORT_UI_OBJECT_ARC,
    PASSPORT_UI_OBJECT_SLIDER,
    PASSPORT_UI_OBJECT_SWITCH,
    PASSPORT_UI_OBJECT_SPINNER,
    PASSPORT_UI_OBJECT_LINE,
    PASSPORT_UI_OBJECT_CHECKBOX,
    PASSPORT_UI_OBJECT_CANVAS,
} passport_ui_object_kind_t;

/** Configure system UI state. battery_available controls status-bar battery polling. */
void passport_ui_init(bool battery_available);
/** Rebuild shared LVGL styles after passport_theme_apply(). Call with the LVGL lock held. */
void passport_ui_theme_refresh(void);

/** Shared 14 px Chinese UI font. Apps do not select arbitrary font sizes. */
const lv_font_t *passport_ui_font(void);

/** Create a 240x320 system page with optional status and action-hint bars. */
passport_page_t *passport_ui_page_create(const char *title, bool show_status_bar, bool show_key_bar);
void passport_ui_page_destroy(passport_page_t *page);
void passport_ui_page_show(passport_page_t *page);

void passport_ui_page_set_status_bar(passport_page_t *page, bool visible);
void passport_ui_page_set_key_bar(passport_page_t *page, bool visible);

/**
 * Set the action noun shown after the system-owned OK prefix.
 * UP/DOWN and the "(选择)" navigation hint are fixed by the page container.
 * The navigation component owns the long-OK Back/Home hint and behavior.
 */
void passport_ui_page_set_action(passport_page_t *page, const char *ok_action);
void passport_ui_page_set_can_go_back(passport_page_t *page, bool can_go_back);

/** Theme-aware, bounded primitives used by the PAP runtime. */
lv_obj_t *passport_ui_object_create(passport_page_t *page, lv_obj_t *parent,
                                    passport_ui_object_kind_t kind,
                                    const char *text,
                                    passport_style_id_t style);
bool passport_ui_object_set_text(lv_obj_t *object,
                                 passport_ui_object_kind_t kind,
                                 const char *text);
bool passport_ui_object_set_value(lv_obj_t *object,
                                  passport_ui_object_kind_t kind,
                                  int32_t value, bool animate);
bool passport_ui_object_set_range(lv_obj_t *object,
                                  passport_ui_object_kind_t kind,
                                  int32_t minimum, int32_t maximum);
bool passport_ui_object_set_checked(lv_obj_t *object,
                                    passport_ui_object_kind_t kind,
                                    bool checked);
bool passport_ui_object_set_selected(lv_obj_t *object,
                                     passport_ui_object_kind_t kind,
                                     bool selected);
bool passport_ui_object_set_pressed(lv_obj_t *object,
                                    passport_ui_object_kind_t kind,
                                    bool pressed);
bool passport_ui_object_replace_style(lv_obj_t *object,
                                      passport_style_id_t old_style,
                                      passport_style_id_t new_style);
bool passport_ui_object_set_property(lv_obj_t *object,
                                     passport_style_property_t property,
                                     int32_t value);

/** Create theme-inheriting standard labels inside the page content. */
lv_obj_t *passport_ui_label_create(passport_page_t *page, const char *text);
void passport_ui_label_set_text(lv_obj_t *label, const char *text);

/** Compact selectable list used by Launcher and system apps. */
passport_ui_list_t *passport_ui_list_create(passport_page_t *page, size_t capacity);
void passport_ui_list_destroy(passport_ui_list_t *list);
bool passport_ui_list_add(passport_ui_list_t *list, const char *text);
/** Add a settings-style row with a right-aligned value. */
bool passport_ui_list_add_value(passport_ui_list_t *list,
                                const char *text,
                                const char *value);
bool passport_ui_list_set_value(passport_ui_list_t *list,
                                size_t index,
                                const char *value);
void passport_ui_list_move(passport_ui_list_t *list, int delta);
size_t passport_ui_list_selected(const passport_ui_list_t *list);
