#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_CODE_COMPACT_LENGTH 10U
#define DEVICE_CODE_DISPLAY_LENGTH 11U

/*
 * Encode all 48 factory-MAC bits into ten Crockford Base32 characters.
 * The mapping is injective: different MAC addresses cannot produce the same code.
 */
void device_code_from_mac(const uint8_t mac[6], char compact[11], char display[12]);
bool device_code_is_valid(const char *compact);
