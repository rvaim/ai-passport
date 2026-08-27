#pragma once

#include <stdbool.h>

/** Stable PAP key values. Bit flags reserve room for future chord-capable boards. */
typedef enum {
    PASSPORT_INPUT_KEY_UP = 1 << 0,
    PASSPORT_INPUT_KEY_DOWN = 1 << 1,
    PASSPORT_INPUT_KEY_OK = 1 << 2,
} passport_input_key_t;

typedef enum {
    PASSPORT_INPUT_EVENT_PRESS = 0,
    PASSPORT_INPUT_EVENT_CLICK,
    PASSPORT_INPUT_EVENT_DOUBLE_CLICK,
    PASSPORT_INPUT_EVENT_LONG_PRESS,
} passport_input_event_t;

/** The current ESP32-C3 board uses one ADC ladder and cannot identify chords. */
static inline bool passport_input_chords_supported(void)
{
    return false;
}
