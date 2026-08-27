#include "passport_package.h"

#include "passport_crc32.h"
#include "passport_storage.h"
#include "esp_log.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "passport_package";
static const uint8_t PAP_MAGIC[4] = {'P', 'A', 'P', '1'};
#define PAP_HEADER_SIZE 16U
#define PAP_ENTRY_HEADER_SIZE 12U
#define PAP_COPY_CHUNK 512U

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static esp_err_t join_path(char *out, size_t capacity, const char *root, const char *relative)
{
    if (!out || capacity == 0 || !root || !relative) return ESP_ERR_INVALID_ARG;
    size_t root_len = strlen(root);
    size_t relative_len = strlen(relative);
    if (root_len == 0 || root_len >= capacity || relative_len >= capacity - root_len - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, root, root_len);
    out[root_len] = '/';
    memcpy(out + root_len + 1, relative, relative_len + 1);
    return ESP_OK;
}

static esp_err_t read_header(FILE *f, passport_package_kind_t *kind, uint32_t *manifest_len, uint32_t *entry_count)
{
    uint8_t h[PAP_HEADER_SIZE];
    if (fread(h, 1, sizeof(h), f) != sizeof(h)) return ESP_ERR_INVALID_SIZE;
    if (memcmp(h, PAP_MAGIC, sizeof(PAP_MAGIC)) != 0 || read_le16(h + 4) != PASSPORT_PACKAGE_FORMAT_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    uint16_t k = read_le16(h + 6);
    if (k != PASSPORT_PACKAGE_APP && k != PASSPORT_PACKAGE_THEME) return ESP_ERR_INVALID_ARG;
    uint32_t mlen = read_le32(h + 8);
    uint32_t count = read_le32(h + 12);
    if (mlen == 0 || mlen > PASSPORT_PACKAGE_MANIFEST_MAX ||
        count > PASSPORT_PACKAGE_MAX_ENTRIES) {
        return ESP_ERR_INVALID_SIZE;
    }
    *kind = (passport_package_kind_t)k;
    *manifest_len = mlen;
    *entry_count = count;
    return ESP_OK;
}

static esp_err_t read_manifest(FILE *f, passport_package_kind_t kind, uint32_t manifest_len,
                               passport_manifest_t *manifest, char **json_out)
{
    char *json = malloc((size_t)manifest_len + 1);
    if (!json) return ESP_ERR_NO_MEM;
    if (fread(json, 1, manifest_len, f) != manifest_len) {
        free(json);
        return ESP_ERR_INVALID_SIZE;
    }
    json[manifest_len] = '\0';
    esp_err_t err = passport_package_parse_manifest_json(
        json, manifest_len, kind, manifest);
    if (err != ESP_OK) {
        free(json);
        return err;
    }
    *json_out = json;
    return ESP_OK;
}

static esp_err_t ensure_parent_dirs(const char *root, const char *relative)
{
    char path[256];
    esp_err_t err = join_path(path, sizeof(path), root, relative);
    if (err != ESP_OK) return err;
    for (char *p = path + strlen(root) + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) return ESP_FAIL;
        *p = '/';
    }
    return ESP_OK;
}

static esp_err_t copy_entry(FILE *package, const char *stage_root, uint32_t *payload_bytes)
{
    uint8_t eh[PAP_ENTRY_HEADER_SIZE];
    if (fread(eh, 1, sizeof(eh), package) != sizeof(eh)) return ESP_ERR_INVALID_SIZE;
    uint16_t path_len = read_le16(eh);
    uint16_t flags = read_le16(eh + 2);
    uint32_t size = read_le32(eh + 4);
    uint32_t expected_crc = read_le32(eh + 8);
    if (path_len == 0 || path_len >= PASSPORT_PACKAGE_PATH_MAX || flags != 0U ||
        size > PASSPORT_PACKAGE_ENTRY_MAX_BYTES) return ESP_ERR_INVALID_SIZE;

    char relative[PASSPORT_PACKAGE_PATH_MAX];
    if (fread(relative, 1, path_len, package) != path_len) return ESP_ERR_INVALID_SIZE;
    relative[path_len] = '\0';
    if (!passport_package_path_is_safe(relative) || strcmp(relative, "manifest.json") == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ensure_parent_dirs(stage_root, relative);
    if (err != ESP_OK) return err;

    char dest[256];
    err = join_path(dest, sizeof(dest), stage_root, relative);
    if (err != ESP_OK) return err;
    struct stat existing;
    if (stat(dest, &existing) == 0) return ESP_ERR_INVALID_ARG;
    FILE *out = fopen(dest, "wb");
    if (!out) return ESP_FAIL;

    uint8_t *buffer = malloc(PAP_COPY_CHUNK);
    if (!buffer) {
        fclose(out);
        unlink(dest);
        return ESP_ERR_NO_MEM;
    }
    uint32_t remaining = size;
    uint32_t crc = 0;
    while (remaining) {
        size_t want = remaining > PAP_COPY_CHUNK ? PAP_COPY_CHUNK : remaining;
        if (fread(buffer, 1, want, package) != want || fwrite(buffer, 1, want, out) != want) {
            err = ESP_FAIL;
            break;
        }
        crc = passport_crc32_update(crc, buffer, want);
        remaining -= (uint32_t)want;
    }
    free(buffer);
    if (fclose(out) != 0 && err == ESP_OK) err = ESP_FAIL;
    if (err == ESP_OK && crc != expected_crc) err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) unlink(dest);
    else *payload_bytes += size;
    return err;
}

static esp_err_t write_manifest_file(const char *stage_root, const char *json, size_t len)
{
    char path[256];
    esp_err_t err = join_path(path, sizeof(path), stage_root, "manifest.json");
    if (err != ESP_OK) return err;
    FILE *f = fopen(path, "wb");
    if (!f) return ESP_FAIL;
    bool ok = fwrite(json, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}

static const char *kind_dir(passport_package_kind_t kind)
{
    return kind == PASSPORT_PACKAGE_APP ? PASSPORT_APPS_DIR : PASSPORT_THEMES_DIR;
}

esp_err_t passport_package_install(const char *package_path, passport_package_result_t *out)
{
    if (!package_path) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(package_path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    passport_package_kind_t kind;
    uint32_t manifest_len, entry_count;
    passport_manifest_t manifest;
    char *manifest_json = NULL;
    esp_err_t err = read_header(f, &kind, &manifest_len, &entry_count);
    if (err == ESP_OK) err = read_manifest(f, kind, manifest_len, &manifest, &manifest_json);
    if (err != ESP_OK) {
        fclose(f);
        return err;
    }

    char stage[256], final[256], backup[256];
    char backup_name[PASSPORT_MANIFEST_ID_MAX + sizeof(".backup")];
    size_t id_len = strlen(manifest.id);
    backup_name[0] = '.';
    memcpy(backup_name + 1, manifest.id, id_len);
    memcpy(backup_name + 1 + id_len, ".backup", sizeof(".backup"));
    err = join_path(stage, sizeof(stage), PASSPORT_STAGING_DIR, manifest.id);
    if (err == ESP_OK) err = join_path(final, sizeof(final), kind_dir(kind), manifest.id);
    if (err == ESP_OK) err = join_path(backup, sizeof(backup), kind_dir(kind), backup_name);
    if (err != ESP_OK) {
        free(manifest_json);
        fclose(f);
        return err;
    }
    passport_storage_remove_tree(stage);
    passport_storage_remove_tree(backup);
    if (mkdir(stage, 0755) != 0) err = ESP_FAIL;
    if (err == ESP_OK) err = write_manifest_file(stage, manifest_json, manifest_len);

    uint32_t payload_bytes = 0;
    for (uint32_t i = 0; err == ESP_OK && i < entry_count; ++i) {
        err = copy_entry(f, stage, &payload_bytes);
    }
    if (err == ESP_OK) {
        int trailing = fgetc(f);
        if (trailing != EOF) err = ESP_ERR_INVALID_SIZE;
        else if (ferror(f)) err = ESP_FAIL;
    }
    free(manifest_json);
    fclose(f);

    if (err == ESP_OK && kind == PASSPORT_PACKAGE_APP) {
        char entry_path[256];
        err = join_path(entry_path, sizeof(entry_path), stage, manifest.entry);
        if (err == ESP_OK) {
            struct stat st;
            if (stat(entry_path, &st) != 0 || !S_ISREG(st.st_mode)) err = ESP_ERR_NOT_FOUND;
        }
    }
    if (err != ESP_OK) {
        passport_storage_remove_tree(stage);
        return err;
    }

    struct stat old_st;
    bool had_old = stat(final, &old_st) == 0;
    if (had_old && rename(final, backup) != 0) {
        passport_storage_remove_tree(stage);
        return ESP_FAIL;
    }
    if (rename(stage, final) != 0) {
        if (had_old) rename(backup, final);
        passport_storage_remove_tree(stage);
        return ESP_FAIL;
    }
    if (had_old) passport_storage_remove_tree(backup);

    if (out) {
        memset(out, 0, sizeof(*out));
        out->manifest = manifest;
        out->files_installed = entry_count + 1;
        out->payload_bytes = payload_bytes + manifest_len;
    }
    ESP_LOGI(TAG, "已安装 %s %s (%s)", kind == PASSPORT_PACKAGE_APP ? "插件" : "主题",
             manifest.name, manifest.version);
    return ESP_OK;
}

esp_err_t passport_package_uninstall(passport_package_kind_t kind, const char *id)
{
    if ((kind != PASSPORT_PACKAGE_APP && kind != PASSPORT_PACKAGE_THEME) ||
        !passport_package_id_is_valid(id)) return ESP_ERR_INVALID_ARG;
    char path[256];
    esp_err_t err = join_path(path, sizeof(path), kind_dir(kind), id);
    if (err != ESP_OK) return err;
    return passport_storage_remove_tree(path);
}
