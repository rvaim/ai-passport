#include "passport_settings.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    passport_settings_snapshot_t defaults;
    passport_settings_model_defaults(&defaults);
    assert(defaults.brightness_percent == 50U);
    assert(defaults.volume_percent == 30U);
    assert(defaults.screen_timeout_seconds == 30U);
    assert(!defaults.key_sound_enabled);

    assert(passport_settings_model_value_valid(PASSPORT_SETTING_BRIGHTNESS, 10U));
    assert(passport_settings_model_value_valid(PASSPORT_SETTING_BRIGHTNESS, 100U));
    assert(!passport_settings_model_value_valid(PASSPORT_SETTING_BRIGHTNESS, 0U));
    assert(!passport_settings_model_value_valid(PASSPORT_SETTING_BRIGHTNESS, 55U));
    assert(passport_settings_model_next(PASSPORT_SETTING_BRIGHTNESS, 50U) == 60U);
    assert(passport_settings_model_next(PASSPORT_SETTING_BRIGHTNESS, 100U) == 10U);

    assert(passport_settings_model_value_valid(PASSPORT_SETTING_VOLUME, 0U));
    assert(passport_settings_model_next(PASSPORT_SETTING_VOLUME, 30U) == 40U);
    assert(passport_settings_model_next(PASSPORT_SETTING_VOLUME, 100U) == 0U);

    assert(passport_settings_model_next(PASSPORT_SETTING_SCREEN_TIMEOUT, 30U) == 60U);
    assert(passport_settings_model_next(PASSPORT_SETTING_SCREEN_TIMEOUT, 60U) == 180U);
    assert(passport_settings_model_next(PASSPORT_SETTING_SCREEN_TIMEOUT, 180U) == 300U);
    assert(passport_settings_model_next(PASSPORT_SETTING_SCREEN_TIMEOUT, 300U) == 0U);
    assert(passport_settings_model_next(PASSPORT_SETTING_SCREEN_TIMEOUT, 0U) == 30U);
    assert(!passport_settings_model_value_valid(PASSPORT_SETTING_SCREEN_TIMEOUT, 45U));

    assert(passport_settings_model_next(PASSPORT_SETTING_KEY_SOUND, 0U) == 1U);
    assert(passport_settings_model_next(PASSPORT_SETTING_KEY_SOUND, 1U) == 0U);
    assert(passport_settings_model_next((passport_setting_id_t)99, 0U) == 0U);

    assert(!passport_settings_model_timeout_due(100U, 90U, 11U));
    assert(passport_settings_model_timeout_due(101U, 90U, 11U));
    assert(passport_settings_model_timeout_due(4U, UINT32_MAX - 5U, 10U));
    assert(!passport_settings_model_timeout_due(100U, 0U, 0U));

    passport_settings_wake_guard_t guard = {0};
    assert(passport_settings_model_consume_wake(&guard, 1U, true, false, true));
    assert(guard.suppress_sequence);
    assert(passport_settings_model_consume_wake(&guard, 1U, false, true, false));
    assert(!guard.suppress_sequence);
    assert(!passport_settings_model_consume_wake(&guard, 1U, true, false, false));
    assert(passport_settings_model_consume_wake(&guard, 1U, false, true, true));

    guard = (passport_settings_wake_guard_t) {
        .suppress_sequence = true,
        .button = 1U,
    };
    assert(!passport_settings_model_consume_wake(&guard, 2U, false, true, false));
    assert(guard.suppress_sequence);
    assert(!passport_settings_model_consume_wake(&guard, 1U, true, false, false));
    assert(!guard.suppress_sequence);

    puts("Passport settings model host tests: PASS");
    return 0;
}
