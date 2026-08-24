#include "device_code.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t first[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t second[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t device[6] = {0x30, 0xed, 0xa0, 0x12, 0x34, 0x56};
    char first_compact[11];
    char first_display[12];
    char second_compact[11];
    char second_display[12];
    char device_compact[11];
    char device_display[12];

    device_code_from_mac(first, first_compact, first_display);
    device_code_from_mac(second, second_compact, second_display);
    device_code_from_mac(device, device_compact, device_display);

    assert(strcmp(first_compact, "0000000000") == 0);
    assert(strcmp(first_display, "00000-00000") == 0);
    assert(strcmp(second_compact, "7ZZZZZZZZZ") == 0);
    assert(strcmp(second_display, "7ZZZZ-ZZZZZ") == 0);
    assert(strcmp(first_compact, second_compact) != 0);
    assert(device_code_is_valid(device_compact));
    assert(device_display[5] == '-');
    assert(strlen(device_display) == DEVICE_CODE_DISPLAY_LENGTH);
    assert(!device_code_is_valid("O000000000"));
    assert(!device_code_is_valid("SHORT"));

    puts("device code tests passed");
    return 0;
}
