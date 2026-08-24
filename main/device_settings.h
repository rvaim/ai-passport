#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

esp_err_t device_settings_init(bool audio_available);
bool device_settings_get(uint8_t setting_id, int32_t *value);
bool device_settings_set(uint8_t setting_id, int32_t value);

// Returns true when this input only woke a sleeping display and must be consumed.
bool device_settings_note_activity(void);
void device_settings_key_feedback(void);
