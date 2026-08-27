#pragma once

#include <stdarg.h>

static inline void host_esp_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

#define ESP_LOGE(...) host_esp_log(__VA_ARGS__)
#define ESP_LOGW(...) host_esp_log(__VA_ARGS__)
#define ESP_LOGI(...) host_esp_log(__VA_ARGS__)
