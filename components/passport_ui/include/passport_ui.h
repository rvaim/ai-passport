#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct passport_page passport_page_t;
typedef struct passport_ui_list passport_ui_list_t;

/** Configure system UI state. battery_available controls status-bar battery polling. */
void passport_ui_init(bool battery_available);

/** Shared 14 px Chinese UI font. Apps do not select arbitrary font sizes. */
const lv_font_t *passport_ui_font(void);

/** Create a 240x320 system page with optional status and action-hint bars. */
passport_page_t *passport_ui_page_create(const char *title, bool show_status_bar, bool show_key_bar);
void passport_ui_page_destroy(passport_page_t *page);
void passport_ui_page_show(passport_page_t *page);

void passport_ui_page_set_status_bar(passport_page_t *page, bool visible);
void passport_ui_page_set_key_bar(passport_page_t *page, bool visible);

/**
 * Set the action nouns shown after the system-owned OK and long-OK prefixes.
 * UP/DOWN and the "(选择)" navigation hint are fixed by the page container.
 * This changes hint text only; the system keeps ownership of long-OK Home.
 */
void passport_ui_page_set_actions(passport_page_t *page,
                                  const char *ok_action,
                                  const char *long_ok_action);

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
