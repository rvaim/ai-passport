#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_DEFAULT_BRIGHTNESS_PERCENT 50U
#define PASSPORT_DEFAULT_VOLUME_PERCENT 30U
#define PASSPORT_DEFAULT_SCREEN_TIMEOUT_SECONDS 30U
#define PASSPORT_DEFAULT_KEY_SOUND_ENABLED false

typedef enum {
    PASSPORT_SETTING_BRIGHTNESS = 0,
    PASSPORT_SETTING_VOLUME,
    PASSPORT_SETTING_SCREEN_TIMEOUT,
    PASSPORT_SETTING_KEY_SOUND,
    PASSPORT_SETTING_COUNT,
} passport_setting_id_t;

typedef struct {
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint16_t screen_timeout_seconds;
    bool key_sound_enabled;
} passport_settings_snapshot_t;

typedef struct {
    bool suppress_sequence;
    uint8_t button;
} passport_settings_wake_guard_t;

/** Pure settings model helpers. They do not access ESP-IDF, NVS, or hardware. */
void passport_settings_model_defaults(passport_settings_snapshot_t *out);
bool passport_settings_model_value_valid(passport_setting_id_t id, uint16_t value);
uint16_t passport_settings_model_next(passport_setting_id_t id, uint16_t current);
bool passport_settings_model_timeout_due(uint32_t now_ticks,
                                         uint32_t last_activity_ticks,
                                         uint32_t timeout_ticks);
bool passport_settings_model_consume_wake(passport_settings_wake_guard_t *guard,
                                          uint8_t button,
                                          bool is_press,
                                          bool is_terminal,
                                          bool woke_display);

/**
 * Load persistent settings, apply display brightness, and start the bounded
 * persistence/timeout/audio worker. NVS must already be initialized and the
 * display backlight driver must already exist.
 */
esp_err_t passport_settings_init(void);

bool passport_settings_get(passport_setting_id_t id, uint16_t *out_value);
void passport_settings_get_snapshot(passport_settings_snapshot_t *out);
esp_err_t passport_settings_set(passport_setting_id_t id, uint16_t value);
esp_err_t passport_settings_cycle(passport_setting_id_t id);

/**
 * Record user input and wake a timed-out display. Returns true only when this
 * input woke the display and its remaining click/long sequence must be eaten.
 */
bool passport_settings_note_activity(void);

/** Queue one non-blocking key click. The worker lazily initializes audio. */
void passport_settings_key_feedback(void);

/** Queue one volume preview even when the ordinary key-sound switch is off. */
void passport_settings_sound_preview(void);
