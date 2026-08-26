#pragma once

#include <stddef.h>
#include <stdint.h>

/** Incremental IEEE CRC-32 used by .pap packages and Passport Link frames. */
uint32_t passport_crc32_update(uint32_t crc, const void *data, size_t len);

/** Convenience one-shot IEEE CRC-32. */
uint32_t passport_crc32(const void *data, size_t len);
