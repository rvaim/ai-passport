#include "device_code.h"

#include <stddef.h>
#include <string.h>

static const char ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void device_code_from_mac(const uint8_t mac[6], char compact[11], char display[12])
{
    uint64_t value = 0;

    if (!mac || !compact || !display) return;
    for (size_t index = 0; index < 6U; ++index) {
        value = (value << 8U) | mac[index];
    }
    for (size_t index = DEVICE_CODE_COMPACT_LENGTH; index > 0U; --index) {
        compact[index - 1U] = ALPHABET[value & 0x1fU];
        value >>= 5U;
    }
    compact[DEVICE_CODE_COMPACT_LENGTH] = '\0';
    memcpy(display, compact, 5U);
    display[5] = '-';
    memcpy(display + 6U, compact + 5U, 5U);
    display[DEVICE_CODE_DISPLAY_LENGTH] = '\0';
}
bool device_code_is_valid(const char *compact)
{
    if (!compact || strlen(compact) != DEVICE_CODE_COMPACT_LENGTH) return false;
    for (size_t index = 0; index < DEVICE_CODE_COMPACT_LENGTH; ++index) {
        if (!strchr(ALPHABET, compact[index])) return false;
    }
    return true;
}
