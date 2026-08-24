#include "device_identity.h"

#include "device_code.h"

#include "esp_log.h"
#include "esp_mac.h"

#include <string.h>

static const char *TAG = "device_identity";
static char s_compact[11];
static char s_display[12];
static bool s_ready;

esp_err_t device_identity_init(void)
{
    uint8_t mac[6];

    if (s_ready) return ESP_OK;
    esp_err_t result = esp_efuse_mac_get_default(mac);
    if (result != ESP_OK) return result;
    device_code_from_mac(mac, s_compact, s_display);
    if (!device_code_is_valid(s_compact)) return ESP_FAIL;
    s_ready = true;
    ESP_LOGI(TAG, "device code: %s", s_display);
    return ESP_OK;
}
const char *device_identity_code(void)
{
    return s_ready ? s_display : "-----";
}

const char *device_identity_code_compact(void)
{
    return s_ready ? s_compact : "";
}

bool device_identity_matches(const void *code, size_t size)
{
    return s_ready && code && size == DEVICE_CODE_COMPACT_LENGTH &&
           memcmp(code, s_compact, DEVICE_CODE_COMPACT_LENGTH) == 0;
}
