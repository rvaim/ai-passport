#include "passport_settings.h"

void passport_settings_model_defaults(passport_settings_snapshot_t *out)
{
    if (!out) return;
    *out = (passport_settings_snapshot_t) {
        .brightness_percent = PASSPORT_DEFAULT_BRIGHTNESS_PERCENT,
        .volume_percent = PASSPORT_DEFAULT_VOLUME_PERCENT,
        .screen_timeout_seconds = PASSPORT_DEFAULT_SCREEN_TIMEOUT_SECONDS,
        .key_sound_enabled = PASSPORT_DEFAULT_KEY_SOUND_ENABLED,
    };
}

bool passport_settings_model_value_valid(passport_setting_id_t id, uint16_t value)
{
    switch (id) {
    case PASSPORT_SETTING_BRIGHTNESS:
        return value >= 10U && value <= 100U && value % 10U == 0U;
    case PASSPORT_SETTING_VOLUME:
        return value <= 100U && value % 10U == 0U;
    case PASSPORT_SETTING_SCREEN_TIMEOUT:
        return value == 0U || value == 30U || value == 60U ||
               value == 180U || value == 300U;
    case PASSPORT_SETTING_KEY_SOUND:
        return value <= 1U;
    default:
        return false;
    }
}

uint16_t passport_settings_model_next(passport_setting_id_t id, uint16_t current)
{
    if (!passport_settings_model_value_valid(id, current)) {
        passport_settings_snapshot_t defaults;
        passport_settings_model_defaults(&defaults);
        switch (id) {
        case PASSPORT_SETTING_BRIGHTNESS: return defaults.brightness_percent;
        case PASSPORT_SETTING_VOLUME: return defaults.volume_percent;
        case PASSPORT_SETTING_SCREEN_TIMEOUT: return defaults.screen_timeout_seconds;
        case PASSPORT_SETTING_KEY_SOUND: return defaults.key_sound_enabled ? 1U : 0U;
        default: return 0U;
        }
    }

    switch (id) {
    case PASSPORT_SETTING_BRIGHTNESS:
        return current >= 100U ? 10U : current + 10U;
    case PASSPORT_SETTING_VOLUME:
        return current >= 100U ? 0U : current + 10U;
    case PASSPORT_SETTING_SCREEN_TIMEOUT:
        if (current == 30U) return 60U;
        if (current == 60U) return 180U;
        if (current == 180U) return 300U;
        if (current == 300U) return 0U;
        return 30U;
    case PASSPORT_SETTING_KEY_SOUND:
        return current ? 0U : 1U;
    default:
        return 0U;
    }
}

bool passport_settings_model_timeout_due(uint32_t now_ticks,
                                         uint32_t last_activity_ticks,
                                         uint32_t timeout_ticks)
{
    return timeout_ticks != 0U &&
           (uint32_t)(now_ticks - last_activity_ticks) >= timeout_ticks;
}

bool passport_settings_model_consume_wake(passport_settings_wake_guard_t *guard,
                                          uint8_t button,
                                          bool is_press,
                                          bool is_terminal,
                                          bool woke_display)
{
    if (!guard) return woke_display;
    if (is_press && guard->suppress_sequence) {
        /* A second press starts a new physical sequence if a terminal event was lost. */
        guard->suppress_sequence = false;
    }
    if (woke_display) {
        if (is_press) {
            guard->suppress_sequence = true;
            guard->button = button;
        }
        return true;
    }
    if (!guard->suppress_sequence || guard->button != button) return false;
    if (is_terminal) guard->suppress_sequence = false;
    return true;
}
