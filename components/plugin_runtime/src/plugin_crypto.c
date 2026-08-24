#include "plugin_crypto.h"

#include "plugin_trust_key.h"

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

#include <string.h>

#define HASH_CHUNK_SIZE 1024U

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;

    for (size_t index = 0; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static esp_err_t calculate_digest(const esp_partition_t *partition, size_t offset,
                                  const plugin_package_header_t *header,
                                  const uint8_t prefix[12], uint8_t digest[32])
{
    mbedtls_sha256_context context;
    uint8_t chunk[HASH_CHUNK_SIZE];
    size_t remaining = header->content_size;
    size_t cursor = offset + header->header_size;
    int result;

    mbedtls_sha256_init(&context);
    result = mbedtls_sha256_starts(&context, 0);
    if (result == 0) result = mbedtls_sha256_update(&context, prefix, 12U);
    while (result == 0 && remaining > 0U) {
        size_t length = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        if (esp_partition_read(partition, cursor, chunk, length) != ESP_OK) {
            mbedtls_sha256_free(&context);
            return ESP_FAIL;
        }
        result = mbedtls_sha256_update(&context, chunk, length);
        cursor += length;
        remaining -= length;
    }
    if (result == 0) result = mbedtls_sha256_finish(&context, digest);
    mbedtls_sha256_free(&context);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t verify_signature(const uint8_t digest[32], const uint8_t signature[64])
{
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_key;
    mbedtls_mpi r;
    mbedtls_mpi s;
    int result;

    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&public_key);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_point_read_binary(&group, &public_key,
                                               PLUGIN_TRUSTED_PUBLIC_KEY,
                                               sizeof(PLUGIN_TRUSTED_PUBLIC_KEY));
    }
    if (result == 0) result = mbedtls_mpi_read_binary(&r, signature, 32U);
    if (result == 0) result = mbedtls_mpi_read_binary(&s, signature + 32U, 32U);
    if (result == 0) result = mbedtls_ecdsa_verify(&group, digest, 32U, &public_key, &r, &s);

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&public_key);
    mbedtls_ecp_group_free(&group);
    return result == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t plugin_crypto_verify_partition(const esp_partition_t *partition, size_t offset,
                                         size_t package_size,
                                         plugin_package_header_t *package_header,
                                         plugin_manifest_t *manifest)
{
    uint8_t raw_header[PLUGIN_PACKAGE_HEADER_SIZE];
    uint8_t digest[PLUGIN_PACKAGE_DIGEST_SIZE];
    plugin_package_header_t parsed_header;
    plugin_manifest_t parsed_manifest;
    plugin_format_result_t format_result;
    const void *mapped_content = NULL;
    esp_partition_mmap_handle_t mapping = 0;
    esp_err_t result;

    if (!partition || package_size < PLUGIN_PACKAGE_HEADER_SIZE ||
        offset > partition->size || package_size > partition->size - offset) {
        return ESP_ERR_INVALID_ARG;
    }
    result = esp_partition_read(partition, offset, raw_header, sizeof(raw_header));
    if (result != ESP_OK) return result;
    format_result = plugin_format_parse_package_header(raw_header, package_size, &parsed_header);
    if (format_result != PLUGIN_FORMAT_OK ||
        (uint64_t)parsed_header.header_size + parsed_header.content_size != package_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    result = calculate_digest(partition, offset, &parsed_header, raw_header, digest);
    if (result != ESP_OK) return result;
    if (!constant_time_equal(digest, parsed_header.digest, sizeof(digest))) {
        return ESP_ERR_INVALID_CRC;
    }
    result = verify_signature(digest, parsed_header.signature);
    if (result != ESP_OK) return result;

    result = esp_partition_mmap(partition, offset + parsed_header.header_size,
                                parsed_header.content_size, ESP_PARTITION_MMAP_DATA,
                                &mapped_content, &mapping);
    if (result != ESP_OK) return result;
    format_result = plugin_format_validate_content(mapped_content,
                                                   parsed_header.content_size,
                                                   &parsed_manifest);
    esp_partition_munmap(mapping);
    if (format_result != PLUGIN_FORMAT_OK) return ESP_ERR_INVALID_ARG;

    if (package_header) *package_header = parsed_header;
    if (manifest) *manifest = parsed_manifest;
    return ESP_OK;
}
