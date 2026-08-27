#include "passport_package.h"

#include "passport_manifest_internal.h"
#include "passport_text.h"
#include "passport_theme_parser.h"
#include <stdbool.h>
#include <string.h>

static bool valid_id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-' || c == '_';
}

bool passport_package_id_is_valid(const char *id)
{
    if (!id || !id[0]) return false;
    size_t length = 0;
    while (id[length]) {
        if (length >= PASSPORT_MANIFEST_ID_MAX - 1U || !valid_id_char(id[length])) {
            return false;
        }
        ++length;
    }
    return true;
}

bool passport_package_path_is_safe(const char *path)
{
    if (!path || !path[0] || path[0] == '/' ||
        strlen(path) >= PASSPORT_PACKAGE_PATH_MAX || strchr(path, '\\')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        bool portable = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                        (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
                        *p == '-' || *p == '/';
        if (!portable) return false;
    }
    const char *segment = path;
    for (const char *p = path; ; ++p) {
        if (*p != '/' && *p != '\0') continue;
        size_t length = (size_t)(p - segment);
        if (length == 0U || (length == 1U && segment[0] == '.') ||
            (length == 2U && segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        if (*p == '\0') break;
        segment = p + 1;
    }
    return true;
}

static bool field_is_allowed(const char *name,
                             const char *const *allowed,
                             size_t allowed_count)
{
    if (!name) return false;
    for (size_t i = 0; i < allowed_count; ++i) {
        if (strcmp(name, allowed[i]) == 0) return true;
    }
    return false;
}

static bool object_has_exact_fields(const cJSON *object,
                                    const char *const *allowed,
                                    size_t allowed_count)
{
    if (!cJSON_IsObject(object)) return false;
    size_t count = 0;
    for (const cJSON *child = object->child; child; child = child->next) {
        if (!field_is_allowed(child->string, allowed, allowed_count)) return false;
        ++count;
    }
    return count == allowed_count;
}

static bool trailing_bytes_are_space(const char *current, const char *end)
{
    while (current < end) {
        char c = *current++;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

esp_err_t passport_manifest_parse_document(const char *json, size_t len,
                                           passport_package_kind_t header_kind,
                                           passport_manifest_t *out,
                                           cJSON **document_out)
{
    if (document_out) *document_out = NULL;
    if (!json || len == 0U || len > PASSPORT_PACKAGE_MANIFEST_MAX || !out || !document_out ||
        (header_kind != PASSPORT_PACKAGE_APP && header_kind != PASSPORT_PACKAGE_THEME) ||
        memchr(json, '\0', len) || !passport_text_utf8_is_valid(json, len) ||
        passport_text_json_contains_nul_escape(json, len)) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &parse_end, false);
    if (!root || !parse_end || !trailing_bytes_are_space(parse_end, json + len)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    static const char *const app_fields[] = {
        "type", "id", "name", "version", "api", "runtime", "entry",
    };
    static const char *const theme_fields[] = {
        "type", "id", "name", "version", "api", "tokens",
    };
    const char *const *fields = header_kind == PASSPORT_PACKAGE_APP ? app_fields : theme_fields;
    size_t field_count = header_kind == PASSPORT_PACKAGE_APP ?
                         sizeof(app_fields) / sizeof(app_fields[0]) :
                         sizeof(theme_fields) / sizeof(theme_fields[0]);

    cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *api = cJSON_GetObjectItemCaseSensitive(root, "api");
    cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, "entry");
    cJSON *runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
    cJSON *tokens = cJSON_GetObjectItemCaseSensitive(root, "tokens");
    const char *expected_kind = header_kind == PASSPORT_PACKAGE_APP ? "app" : "theme";

    bool ok = object_has_exact_fields(root, fields, field_count) &&
              cJSON_IsString(kind) && strcmp(kind->valuestring, expected_kind) == 0 &&
              cJSON_IsString(id) && passport_package_id_is_valid(id->valuestring) &&
              cJSON_IsString(name) && name->valuestring[0] &&
              strlen(name->valuestring) < PASSPORT_MANIFEST_NAME_MAX &&
              cJSON_IsString(version) && version->valuestring[0] &&
              strlen(version->valuestring) < PASSPORT_MANIFEST_VERSION_MAX &&
              cJSON_IsNumber(api) && api->valuedouble == PASSPORT_SYSTEM_API_VERSION;
    if (header_kind == PASSPORT_PACKAGE_APP) {
        ok = ok && cJSON_IsString(entry) &&
             passport_package_path_is_safe(entry->valuestring) &&
             strlen(entry->valuestring) < PASSPORT_MANIFEST_ENTRY_MAX &&
             cJSON_IsString(runtime) && strcmp(runtime->valuestring, "lua") == 0;
    } else {
        ok = ok && cJSON_IsObject(tokens);
    }
    if (!ok) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->kind = header_kind;
    memcpy(out->id, id->valuestring, strlen(id->valuestring) + 1U);
    memcpy(out->name, name->valuestring, strlen(name->valuestring) + 1U);
    memcpy(out->version, version->valuestring, strlen(version->valuestring) + 1U);
    if (header_kind == PASSPORT_PACKAGE_APP) {
        memcpy(out->entry, entry->valuestring, strlen(entry->valuestring) + 1U);
    }
    out->api = PASSPORT_SYSTEM_API_VERSION;
    *document_out = root;
    return ESP_OK;
}

esp_err_t passport_package_parse_manifest_json(const char *json, size_t len,
                                               passport_package_kind_t header_kind,
                                               passport_manifest_t *out)
{
    if (header_kind == PASSPORT_PACKAGE_THEME) {
        passport_theme_tokens_t tokens;
        return passport_theme_parse_manifest_json(json, len, out, &tokens);
    }
    cJSON *document = NULL;
    esp_err_t err = passport_manifest_parse_document(
        json, len, header_kind, out, &document);
    cJSON_Delete(document);
    return err;
}
