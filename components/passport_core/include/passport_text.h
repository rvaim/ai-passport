#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Return true when the complete byte span is canonical UTF-8. */
bool passport_text_utf8_is_valid(const char *text, size_t length);

/** Detect a JSON string escape that would decode to the unsupported U+0000. */
bool passport_text_json_contains_nul_escape(const char *json, size_t length);
