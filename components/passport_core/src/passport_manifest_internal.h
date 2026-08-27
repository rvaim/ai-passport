#pragma once

#include "passport_package.h"
#include "cJSON.h"

/**
 * Parse and validate one current-schema manifest while retaining the cJSON
 * document for a kind-specific validator. The caller owns *document_out.
 */
esp_err_t passport_manifest_parse_document(const char *json, size_t length,
                                            passport_package_kind_t expected_kind,
                                            passport_manifest_t *manifest_out,
                                            cJSON **document_out);
