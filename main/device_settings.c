#include "device_settings.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "plugin_format.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdatomic.h>

#define SETTINGS_NAMESPACE "pass_sys_v4"
#define BRIGHTNESS_KEY "brightness"
#define VOLUME_KEY "volume"
#define KEY_SOUND_KEY "key_sound"
#define SCREEN_TIMEOUT_KEY "screen_off"

#define DEFAULT_BRIGHTNESS 100
#define DEFAULT_VOLUME 50
#define DEFAULT_KEY_SOUND 0
#define DEFAULT_SCREEN_TIMEOUT 0

#define FEEDBACK_SAMPLE_RATE 16000U
#define FEEDBACK_DURATION_MS 18U

typedef struct {
    uint8_t brightness;
    uint8_t volume;
    uint8_t key_sound;
    uint16_t screen_timeout;
} settings_snapshot_t;

static const char *TAG = "device_settings";
static _Atomic int s_values[PLUGIN_SETTING_THEME];
static _Atomic uint32_t s_last_activity_ticks;
static _Atomic bool s_screen_off;
static QueueHandle_t s_save_queue;
static QueueHandle_t s_feedback_queue;
static bool s_audio_available;
static bool s_initialized;

static bool value_valid(uint8_t setting_id, int32_t value)
{
    switch (setting_id) {
    case PLUGIN_SETTING_BRIGHTNESS:
        return value >= 10 && value <= 100 && value % 10 == 0;
    case PLUGIN_SETTING_VOLUME:
        return value >= 0 && value <= 100 && value % 10 == 0;
    case PLUGIN_SETTING_KEY_SOUND:
        return value == 0 || value == 1;
    case PLUGIN_SETTING_SCREEN_TIMEOUT:
        return value == 0 || value == 30 || value == 60 ||
               value == 180 || value == 300;
    default:
        return false;
    }
}

static settings_snapshot_t current_snapshot(void)
{
    return (settings_snapshot_t) {
        .brightness = (uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_BRIGHTNESS], memory_order_relaxed),
        .volume = (uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_VOLUME], memory_order_relaxed),
        .key_sound = (uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_KEY_SOUND], memory_order_relaxed),
        .screen_timeout = (uint16_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_SCREEN_TIMEOUT], memory_order_relaxed),
    };
}

static void persist_snapshot(const settings_snapshot_t *snapshot)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    bool opened = result == ESP_OK;

    if (result == ESP_OK) result = nvs_set_u8(handle, BRIGHTNESS_KEY, snapshot->brightness);
    if (result == ESP_OK) result = nvs_set_u8(handle, VOLUME_KEY, snapshot->volume);
    if (result == ESP_OK) result = nvs_set_u8(handle, KEY_SOUND_KEY, snapshot->key_sound);
    if (result == ESP_OK) {
        result = nvs_set_u16(handle, SCREEN_TIMEOUT_KEY, snapshot->screen_timeout);
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    if (opened) nvs_close(handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "settings were not persisted: %s", esp_err_to_name(result));
    }
}

static void check_screen_timeout(void)
{
    uint32_t timeout_seconds = (uint32_t)atomic_load_explicit(
        &s_values[PLUGIN_SETTING_SCREEN_TIMEOUT], memory_order_relaxed);
    if (timeout_seconds == 0U ||
        atomic_load_explicit(&s_screen_off, memory_order_acquire)) {
        return;
    }

    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t last = atomic_load_explicit(&s_last_activity_ticks, memory_order_acquire);
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_seconds * 1000U);
    if ((uint32_t)(now - last) < timeout_ticks) return;

    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_screen_off, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }
    bsp_display_backlight(0U);

    // An input can race the backlight write. Re-check its timestamp so the input
    // always wins and a just-woken display cannot be turned off again.
    if (atomic_load_explicit(&s_last_activity_ticks, memory_order_acquire) != last ||
        atomic_load_explicit(&s_values[PLUGIN_SETTING_SCREEN_TIMEOUT],
                             memory_order_relaxed) != (int)timeout_seconds) {
        atomic_store_explicit(&s_screen_off, false, memory_order_release);
        bsp_display_backlight((uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_BRIGHTNESS], memory_order_relaxed));
    }
}

static void settings_task(void *argument)
{
    settings_snapshot_t snapshot;
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_save_queue, &snapshot, pdMS_TO_TICKS(250U)) == pdTRUE) {
            if (s_audio_available) bsp_audio_set_volume(snapshot.volume);
            persist_snapshot(&snapshot);
        }
        check_screen_timeout();
    }
}

static void feedback_task(void *argument)
{
    uint8_t signal;
    int16_t samples[96];
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_feedback_queue, &signal, portMAX_DELAY) != pdTRUE) continue;
        if (!bsp_audio_session_begin(80U)) continue;
        if (bsp_audio_set_format(FEEDBACK_SAMPLE_RATE, 16U, 1U) != ESP_OK) {
            bsp_audio_session_end();
            continue;
        }
        bsp_audio_set_volume((uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_VOLUME], memory_order_relaxed));

        uint32_t total = FEEDBACK_SAMPLE_RATE * FEEDBACK_DURATION_MS / 1000U;
        uint32_t offset = 0U;
        while (offset < total) {
            size_t count = total - offset > 96U ? 96U : total - offset;
            for (size_t index = 0; index < count; ++index) {
                uint32_t position = offset + index;
                int32_t amplitude = (int32_t)((total - position) * 2400U / total);
                samples[index] = (position / 3U) % 2U == 0U ? amplitude : -amplitude;
            }
            if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) break;
            offset += count;
        }
        bsp_audio_session_end();
    }
}

static void load_saved_values(void)
{
    uint8_t brightness = DEFAULT_BRIGHTNESS;
    uint8_t volume = DEFAULT_VOLUME;
    uint8_t key_sound = DEFAULT_KEY_SOUND;
    uint16_t screen_timeout = DEFAULT_SCREEN_TIMEOUT;
    nvs_handle_t handle;

    esp_err_t result = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        if (nvs_get_u8(handle, BRIGHTNESS_KEY, &brightness) != ESP_OK ||
            !value_valid(PLUGIN_SETTING_BRIGHTNESS, brightness)) {
            brightness = DEFAULT_BRIGHTNESS;
        }
        if (nvs_get_u8(handle, VOLUME_KEY, &volume) != ESP_OK ||
            !value_valid(PLUGIN_SETTING_VOLUME, volume)) {
            volume = DEFAULT_VOLUME;
        }
        if (nvs_get_u8(handle, KEY_SOUND_KEY, &key_sound) != ESP_OK ||
            !value_valid(PLUGIN_SETTING_KEY_SOUND, key_sound)) {
            key_sound = DEFAULT_KEY_SOUND;
        }
        if (nvs_get_u16(handle, SCREEN_TIMEOUT_KEY, &screen_timeout) != ESP_OK ||
            !value_valid(PLUGIN_SETTING_SCREEN_TIMEOUT, screen_timeout)) {
            screen_timeout = DEFAULT_SCREEN_TIMEOUT;
        }
        nvs_close(handle);
    }

    atomic_store(&s_values[PLUGIN_SETTING_BRIGHTNESS], brightness);
    atomic_store(&s_values[PLUGIN_SETTING_VOLUME], volume);
    atomic_store(&s_values[PLUGIN_SETTING_KEY_SOUND], key_sound);
    atomic_store(&s_values[PLUGIN_SETTING_SCREEN_TIMEOUT], screen_timeout);
}

esp_err_t device_settings_init(bool audio_available)
{
    if (s_initialized) return ESP_OK;
    s_audio_available = audio_available;
    load_saved_values();
    atomic_store(&s_last_activity_ticks, (uint32_t)xTaskGetTickCount());
    atomic_store(&s_screen_off, false);

    bsp_display_backlight((uint8_t)atomic_load(
        &s_values[PLUGIN_SETTING_BRIGHTNESS]));
    if (s_audio_available) {
        bsp_audio_set_volume((uint8_t)atomic_load(&s_values[PLUGIN_SETTING_VOLUME]));
    }

    s_save_queue = xQueueCreate(1U, sizeof(settings_snapshot_t));
    if (!s_save_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(settings_task, "settings", 3072, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_save_queue);
        s_save_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (s_audio_available) {
        s_feedback_queue = xQueueCreate(4U, sizeof(uint8_t));
        if (!s_feedback_queue ||
            xTaskCreate(feedback_task, "key_sound", 3072, NULL, 3, NULL) != pdPASS) {
            if (s_feedback_queue) vQueueDelete(s_feedback_queue);
            s_feedback_queue = NULL;
            ESP_LOGW(TAG, "key sound worker unavailable");
        }
    }
    s_initialized = true;
    return ESP_OK;
}

bool device_settings_get(uint8_t setting_id, int32_t *value)
{
    if (!s_initialized || !value || setting_id >= PLUGIN_SETTING_THEME) return false;
    *value = atomic_load_explicit(&s_values[setting_id], memory_order_relaxed);
    return true;
}

bool device_settings_set(uint8_t setting_id, int32_t value)
{
    if (!s_initialized || setting_id >= PLUGIN_SETTING_THEME ||
        !value_valid(setting_id, value)) return false;
    int previous = atomic_exchange_explicit(&s_values[setting_id], value,
                                            memory_order_acq_rel);
    if (previous == value) return true;

    if (setting_id == PLUGIN_SETTING_BRIGHTNESS &&
        !atomic_load_explicit(&s_screen_off, memory_order_acquire)) {
        bsp_display_backlight((uint8_t)value);
    } else if (setting_id == PLUGIN_SETTING_SCREEN_TIMEOUT) {
        atomic_store_explicit(&s_last_activity_ticks, (uint32_t)xTaskGetTickCount(),
                              memory_order_release);
    }

    settings_snapshot_t snapshot = current_snapshot();
    if (!s_save_queue || xQueueOverwrite(s_save_queue, &snapshot) != pdTRUE) {
        ESP_LOGW(TAG, "settings save queue unavailable");
    }
    return true;
}

bool device_settings_note_activity(void)
{
    if (!s_initialized) return false;
    atomic_store_explicit(&s_last_activity_ticks, (uint32_t)xTaskGetTickCount(),
                          memory_order_release);
    bool woke = atomic_exchange_explicit(&s_screen_off, false, memory_order_acq_rel);
    if (woke) {
        bsp_display_backlight((uint8_t)atomic_load_explicit(
            &s_values[PLUGIN_SETTING_BRIGHTNESS], memory_order_relaxed));
    }
    return woke;
}

void device_settings_key_feedback(void)
{
    uint8_t signal = 1U;
    if (!s_initialized || !s_audio_available || !s_feedback_queue ||
        atomic_load_explicit(&s_values[PLUGIN_SETTING_KEY_SOUND],
                             memory_order_relaxed) == 0) {
        return;
    }
    xQueueSend(s_feedback_queue, &signal, 0);
}
