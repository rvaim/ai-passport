#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

esp_err_t device_identity_init(void);
const char *device_identity_code(void);
const char *device_identity_code_compact(void);
bool device_identity_matches(const void *code, size_t size);
