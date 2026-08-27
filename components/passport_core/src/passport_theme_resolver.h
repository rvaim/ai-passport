#pragma once

#include "passport_theme.h"

/** Resolve built-in defaults, fixed inheritance, and sparse installed layers. */
void passport_theme_resolve(const passport_theme_definition_t *installed,
                            passport_style_t out[PASSPORT_STYLE_COUNT]);
