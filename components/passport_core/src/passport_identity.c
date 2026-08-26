#include "passport_identity.h"

#include "esp_log.h"
#include "esp_mac.h"
#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "passport_identity";
static const char BASE32[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

static uint64_t s_device_id;
static char s_device_code[PASSPORT_DEVICE_CODE_MAX];
static bool s_ready;

void passport_identity_format(uint64_t device_id, char out[PASSPORT_DEVICE_CODE_MAX])
{
    char raw[11];
    uint32_t check = 0;

    /* 48 hardware-identity bits need ten base-32 digits. The final check digit
     * only catches typing errors; it is intentionally not an authentication tag. */
    for (int i = 9; i >= 0; --i) {
        raw[i] = BASE32[device_id & 31U];
        check = (check + (uint32_t)(i + 1) * (uint32_t)(device_id & 31U)) % 32U;
        device_id >>= 5;
    }
    raw[10] = '\0';
    snprintf(out, PASSPORT_DEVICE_CODE_MAX, "%.5s-%.5s-%c", raw, raw + 5, BASE32[check]);
}


static int base32_index(char c)
{
    for (int i = 0; i < 32; ++i) if (BASE32[i] == c) return i;
    return -1;
}

esp_err_t passport_identity_parse_code(const char *code, uint64_t *out_id)
{
    if (!code || !out_id) return ESP_ERR_INVALID_ARG;
    char compact[12];
    size_t n = 0;
    for (const char *p = code; *p; ++p) {
        if (*p == '-') continue;
        if (n >= sizeof(compact) - 1) return ESP_ERR_INVALID_ARG;
        compact[n++] = *p;
    }
    compact[n] = '\0';
    if (n != 11) return ESP_ERR_INVALID_ARG;

    uint64_t value = 0;
    uint32_t check = 0;
    for (int i = 0; i < 10; ++i) {
        int digit = base32_index(compact[i]);
        if (digit < 0) return ESP_ERR_INVALID_ARG;
        value = (value << 5) | (uint32_t)digit;
        check = (check + (uint32_t)(i + 1) * (uint32_t)digit) % 32U;
    }
    int check_digit = base32_index(compact[10]);
    if (check_digit < 0 || (uint32_t)check_digit != check || value > 0xFFFFFFFFFFFFULL) {
        return ESP_ERR_INVALID_CRC;
    }
    *out_id = value;
    return ESP_OK;
}

esp_err_t passport_identity_init(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取工厂 MAC 失败: %s", esp_err_to_name(err));
        return err;
    }

    s_device_id = 0;
    for (size_t i = 0; i < sizeof(mac); ++i) {
        s_device_id = (s_device_id << 8) | mac[i];
    }
    passport_identity_format(s_device_id, s_device_code);
    s_ready = true;
    ESP_LOGI(TAG, "设备码: %s", s_device_code);
    return ESP_OK;
}

uint64_t passport_identity_id(void)
{
    return s_ready ? s_device_id : 0;
}

const char *passport_identity_code(void)
{
    return s_ready ? s_device_code : "未初始化";
}
