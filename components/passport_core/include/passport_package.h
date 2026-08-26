#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_PACKAGE_FORMAT_VERSION 1
#define PASSPORT_PACKAGE_MANIFEST_MAX 4096U
#define PASSPORT_MANIFEST_ID_MAX 48
#define PASSPORT_MANIFEST_NAME_MAX 48
#define PASSPORT_MANIFEST_VERSION_MAX 20
#define PASSPORT_MANIFEST_ENTRY_MAX 96

typedef enum {
    PASSPORT_PACKAGE_APP = 1,
    PASSPORT_PACKAGE_THEME = 2,
} passport_package_kind_t;

typedef struct {
    passport_package_kind_t kind;
    char id[PASSPORT_MANIFEST_ID_MAX];
    char name[PASSPORT_MANIFEST_NAME_MAX];
    char version[PASSPORT_MANIFEST_VERSION_MAX];
    char entry[PASSPORT_MANIFEST_ENTRY_MAX];
    uint32_t api;
} passport_manifest_t;

typedef struct {
    passport_manifest_t manifest;
    uint32_t files_installed;
    uint32_t payload_bytes;
} passport_package_result_t;

/** Validate the portable identifier grammar shared by apps and themes. */
bool passport_package_id_is_valid(const char *id);

/** Parse and validate one UTF-8 manifest JSON document. */
esp_err_t passport_package_parse_manifest_json(const char *json, size_t len,
                                               passport_package_kind_t expected_kind,
                                               passport_manifest_t *out);

/**
 * Transactionally install one .pap file from local storage.
 * The caller owns package_path. Installation uses a staging directory and backup rename.
 */
esp_err_t passport_package_install(const char *package_path, passport_package_result_t *out);

/** Remove an installed app or theme by manifest ID. */
esp_err_t passport_package_uninstall(passport_package_kind_t kind, const char *id);

/** Validate the restricted package path grammar used for payload entries. */
bool passport_package_path_is_safe(const char *path);
