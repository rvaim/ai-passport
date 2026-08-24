#pragma once

#include "plugin_format.h"

#include "esp_err.h"
#include "esp_partition.h"

#include <stddef.h>
#include <stdint.h>

esp_err_t plugin_crypto_verify_partition(const esp_partition_t *partition, size_t offset,
                                         size_t package_size,
                                         plugin_package_header_t *package_header,
                                         plugin_manifest_t *manifest);
