#pragma once

#include "esp_err.h"
#include <stdint.h>

#define PASSPORT_DEVICE_CODE_MAX 16

/**
 * Initialize the immutable public device identity from the factory MAC.
 * The code is an address/anti-misdirection identifier, not a secret.
 */
esp_err_t passport_identity_init(void);

/** 48-bit factory-derived ID stored in the low bits of the return value. */
uint64_t passport_identity_id(void);

/** Human-readable public code, for example 22222-22222-2. */
const char *passport_identity_code(void);

/** Convert a device ID to the canonical public code. Useful for peer discovery. */
void passport_identity_format(uint64_t device_id, char out[PASSPORT_DEVICE_CODE_MAX]);

/** Parse and checksum-validate a public device code back to its 48-bit ID. */
esp_err_t passport_identity_parse_code(const char *code, uint64_t *out_id);
