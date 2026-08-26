#include "passport_settings.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdatomic.h>

#define SETTINGS_NAMESPACE "pass_settings"
#define SETTINGS_SCHEMA_KEY "schema"
#define SETTINGS_SCHEMA_VERSION 1U
#define SETTINGS_BRIGHTNESS_KEY "brightness"
#define SETTINGS_VOLUME_KEY "volume"
#define SETTINGS_TIMEOUT_KEY "screen_off"
#define SETTINGS_KEY_SOUND_KEY "key_sound"

#define SETTINGS_NOTIFY_SAVE (1U << 0)
#define SETTINGS_NOTIFY_SOUND (1U << 1)
#define SETTINGS_NOTIFY_VOLUME (1U << 2)
#define SETTINGS_NOTIFY_PREVIEW (1U << 3)
#define SETTINGS_POLL_MS 250U
#define SETTINGS_WORKER_STACK 3072U

#define FEEDBACK_SAMPLE_RATE 16000U
#define FEEDBACK_DURATION_MS 18U
#define FEEDBACK_CHUNK_SAMPLES 96U
#define FEEDBACK_PEAK_AMPLITUDE 6000

static const char *TAG = "passport_settings";
static _Atomic uint16_t s_values[PASSPORT_SETTING_COUNT];
static _Atomic uint32_t s_last_activity_ticks;
static _Atomic bool s_screen_off;
static TaskHandle_t s_worker;
static bool s_initialized;
static int8_t s_audio_state;

static passport_settings_snapshot_t current_snapshot(void)
{
    return (passport_settings_snapshot_t) {
        .brightness_percent = (uint8_t)atomic_load_explicit(
            &s_values[PASSPORT_SETTING_BRIGHTNESS], memory_order_relaxed),
        .volume_percent = (uint8_t)atomic_load_explicit(
            &s_values[PASSPORT_SETTING_VOLUME], memory_order_relaxed),
        .screen_timeout_seconds = atomic_load_explicit(
            &s_values[PASSPORT_SETTING_SCREEN_TIMEOUT], memory_order_relaxed),
        .key_sound_enabled = atomic_load_explicit(
            &s_values[PASSPORT_SETTING_KEY_SOUND], memory_order_relaxed) != 0U,
    };
}

static esp_err_t persist_to_handle(nvs_handle_t handle,
                                   const passport_settings_snapshot_t *snapshot)
{
    esp_err_t err = nvs_set_u8(handle, SETTINGS_SCHEMA_KEY, SETTINGS_SCHEMA_VERSION);
    if (err == ESP_OK) err = nvs_set_u8(handle, SETTINGS_BRIGHTNESS_KEY,
                                        snapshot->brightness_percent);
    if (err == ESP_OK) err = nvs_set_u8(handle, SETTINGS_VOLUME_KEY,
                                        snapshot->volume_percent);
    if (err == ESP_OK) err = nvs_set_u16(handle, SETTINGS_TIMEOUT_KEY,
                                         snapshot->screen_timeout_seconds);
    if (err == ESP_OK) err = nvs_set_u8(handle, SETTINGS_KEY_SOUND_KEY,
                                        snapshot->key_sound_enabled ? 1U : 0U);
    if (err == ESP_OK) err = nvs_commit(handle);
    return err;
}

static void persist_snapshot(const passport_settings_snapshot_t *snapshot)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = persist_to_handle(handle, snapshot);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "设置保存失败: %s", esp_err_to_name(err));
    }
}

static void load_snapshot(passport_settings_snapshot_t *snapshot)
{
    passport_settings_model_defaults(snapshot);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "设置存储不可用，使用默认值: %s", esp_err_to_name(err));
        return;
    }

    uint8_t schema = 0U;
    bool corrected = nvs_get_u8(handle, SETTINGS_SCHEMA_KEY, &schema) != ESP_OK ||
                     schema != SETTINGS_SCHEMA_VERSION;
    if (!corrected) {
        uint8_t value8;
        uint16_t value16;
        if (nvs_get_u8(handle, SETTINGS_BRIGHTNESS_KEY, &value8) == ESP_OK &&
            passport_settings_model_value_valid(PASSPORT_SETTING_BRIGHTNESS, value8)) {
            snapshot->brightness_percent = value8;
        } else {
            corrected = true;
        }
        if (nvs_get_u8(handle, SETTINGS_VOLUME_KEY, &value8) == ESP_OK &&
            passport_settings_model_value_valid(PASSPORT_SETTING_VOLUME, value8)) {
            snapshot->volume_percent = value8;
        } else {
            corrected = true;
        }
        if (nvs_get_u16(handle, SETTINGS_TIMEOUT_KEY, &value16) == ESP_OK &&
            passport_settings_model_value_valid(PASSPORT_SETTING_SCREEN_TIMEOUT, value16)) {
            snapshot->screen_timeout_seconds = value16;
        } else {
            corrected = true;
        }
        if (nvs_get_u8(handle, SETTINGS_KEY_SOUND_KEY, &value8) == ESP_OK &&
            passport_settings_model_value_valid(PASSPORT_SETTING_KEY_SOUND, value8)) {
            snapshot->key_sound_enabled = value8 != 0U;
        } else {
            corrected = true;
        }
    }

    if (corrected) {
        passport_settings_snapshot_t defaults;
        passport_settings_model_defaults(&defaults);
        *snapshot = defaults;
        err = persist_to_handle(handle, snapshot);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "默认设置写入失败: %s", esp_err_to_name(err));
        }
    }
    nvs_close(handle);
}

static bool ensure_audio_ready(void)
{
    if (s_audio_state > 0) return true;
    if (s_audio_state < 0) return false;
    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) err = bsp_audio_set_format(FEEDBACK_SAMPLE_RATE, 16U, 1U);
    if (err != ESP_OK) {
        s_audio_state = -1;
        ESP_LOGW(TAG, "按键音不可用: %s", esp_err_to_name(err));
        return false;
    }
    s_audio_state = 1;
    return true;
}

static void play_key_sound(void)
{
    const uint16_t volume = atomic_load_explicit(
        &s_values[PASSPORT_SETTING_VOLUME], memory_order_relaxed);
    if (volume == 0U || !ensure_audio_ready()) return;
    bsp_audio_set_volume((uint8_t)volume);

    int16_t samples[FEEDBACK_CHUNK_SAMPLES];
    const uint32_t total = FEEDBACK_SAMPLE_RATE * FEEDBACK_DURATION_MS / 1000U;
    uint32_t offset = 0U;
    while (offset < total) {
        const size_t count = total - offset > FEEDBACK_CHUNK_SAMPLES
            ? FEEDBACK_CHUNK_SAMPLES : total - offset;
        for (size_t index = 0; index < count; ++index) {
            const uint32_t position = offset + index;
            const int32_t amplitude = (int32_t)(
                (uint32_t)FEEDBACK_PEAK_AMPLITUDE * (total - position) / total);
            samples[index] = position % 8U < 4U ? (int16_t)amplitude
                                                : (int16_t)-amplitude;
        }
        if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) break;
        offset += (uint32_t)count;
    }
}

static void check_screen_timeout(void)
{
    const uint32_t timeout_seconds = atomic_load_explicit(
        &s_values[PASSPORT_SETTING_SCREEN_TIMEOUT], memory_order_relaxed);
    if (timeout_seconds == 0U ||
        atomic_load_explicit(&s_screen_off, memory_order_acquire)) {
        return;
    }

    const uint32_t now = (uint32_t)xTaskGetTickCount();
    const uint32_t last = atomic_load_explicit(
        &s_last_activity_ticks, memory_order_acquire);
    const uint32_t timeout_ticks = (uint32_t)pdMS_TO_TICKS(timeout_seconds * 1000U);
    if (!passport_settings_model_timeout_due(now, last, timeout_ticks)) return;

    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_screen_off, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }
    bsp_display_backlight(0U);

    /* Input wins a race with the backlight write and immediately restores it. */
    if (atomic_load_explicit(&s_last_activity_ticks, memory_order_acquire) != last ||
        atomic_load_explicit(&s_values[PASSPORT_SETTING_SCREEN_TIMEOUT],
                             memory_order_relaxed) != timeout_seconds) {
        atomic_store_explicit(&s_screen_off, false, memory_order_release);
        bsp_display_backlight((uint8_t)atomic_load_explicit(
            &s_values[PASSPORT_SETTING_BRIGHTNESS], memory_order_relaxed));
    }
}

static void settings_worker(void *argument)
{
    (void)argument;
    for (;;) {
        uint32_t notifications = 0U;
        xTaskNotifyWait(0U, UINT32_MAX, &notifications,
                        pdMS_TO_TICKS(SETTINGS_POLL_MS));
        const bool preview = (notifications & SETTINGS_NOTIFY_PREVIEW) != 0U;
        const bool key_sound = (notifications & SETTINGS_NOTIFY_SOUND) != 0U &&
            atomic_load_explicit(&s_values[PASSPORT_SETTING_KEY_SOUND],
                                 memory_order_relaxed) != 0U;
        if (preview || key_sound) {
            play_key_sound();
        }
        if ((notifications & SETTINGS_NOTIFY_VOLUME) != 0U && s_audio_state > 0) {
            bsp_audio_set_volume((uint8_t)atomic_load_explicit(
                &s_values[PASSPORT_SETTING_VOLUME], memory_order_relaxed));
        }
        if ((notifications & SETTINGS_NOTIFY_SAVE) != 0U) {
            const passport_settings_snapshot_t snapshot = current_snapshot();
            persist_snapshot(&snapshot);
        }
        check_screen_timeout();
    }
}

esp_err_t passport_settings_init(void)
{
    if (s_initialized) return s_worker ? ESP_OK : ESP_ERR_NO_MEM;
    passport_settings_snapshot_t snapshot;
    load_snapshot(&snapshot);
    atomic_store(&s_values[PASSPORT_SETTING_BRIGHTNESS], snapshot.brightness_percent);
    atomic_store(&s_values[PASSPORT_SETTING_VOLUME], snapshot.volume_percent);
    atomic_store(&s_values[PASSPORT_SETTING_SCREEN_TIMEOUT],
                 snapshot.screen_timeout_seconds);
    atomic_store(&s_values[PASSPORT_SETTING_KEY_SOUND],
                 snapshot.key_sound_enabled ? 1U : 0U);
    atomic_store(&s_last_activity_ticks, (uint32_t)xTaskGetTickCount());
    atomic_store(&s_screen_off, false);
    bsp_display_backlight(snapshot.brightness_percent);
    s_initialized = true;

    if (xTaskCreate(settings_worker, "passport_settings", SETTINGS_WORKER_STACK,
                    NULL, 3, &s_worker) != pdPASS) {
        s_worker = NULL;
        ESP_LOGE(TAG, "设置工作任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "设置就绪: 亮度=%u%% 音量=%u%% 息屏=%us 按键音=%s",
             (unsigned)snapshot.brightness_percent,
             (unsigned)snapshot.volume_percent,
             (unsigned)snapshot.screen_timeout_seconds,
             snapshot.key_sound_enabled ? "开" : "关");
    return ESP_OK;
}

bool passport_settings_get(passport_setting_id_t id, uint16_t *out_value)
{
    if (!s_initialized || !out_value ||
        (unsigned)id >= PASSPORT_SETTING_COUNT) {
        return false;
    }
    *out_value = atomic_load_explicit(&s_values[id], memory_order_relaxed);
    return true;
}

void passport_settings_get_snapshot(passport_settings_snapshot_t *out)
{
    if (!out) return;
    if (!s_initialized) {
        passport_settings_model_defaults(out);
        return;
    }
    *out = current_snapshot();
}

esp_err_t passport_settings_set(passport_setting_id_t id, uint16_t value)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if ((unsigned)id >= PASSPORT_SETTING_COUNT ||
        !passport_settings_model_value_valid(id, value)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t previous = atomic_exchange_explicit(
        &s_values[id], value, memory_order_acq_rel);
    if (previous == value) return ESP_OK;

    if (id == PASSPORT_SETTING_BRIGHTNESS &&
        !atomic_load_explicit(&s_screen_off, memory_order_acquire)) {
        bsp_display_backlight((uint8_t)value);
    } else if (id == PASSPORT_SETTING_SCREEN_TIMEOUT) {
        atomic_store_explicit(&s_last_activity_ticks,
                              (uint32_t)xTaskGetTickCount(), memory_order_release);
    }
    if (s_worker) {
        uint32_t notifications = SETTINGS_NOTIFY_SAVE;
        if (id == PASSPORT_SETTING_VOLUME) notifications |= SETTINGS_NOTIFY_VOLUME;
        xTaskNotify(s_worker, notifications, eSetBits);
    }
    return ESP_OK;
}

esp_err_t passport_settings_cycle(passport_setting_id_t id)
{
    uint16_t current;
    if (!passport_settings_get(id, &current)) return ESP_ERR_INVALID_STATE;
    return passport_settings_set(id, passport_settings_model_next(id, current));
}

bool passport_settings_note_activity(void)
{
    if (!s_initialized) return false;
    atomic_store_explicit(&s_last_activity_ticks,
                          (uint32_t)xTaskGetTickCount(), memory_order_release);
    const bool woke = atomic_exchange_explicit(
        &s_screen_off, false, memory_order_acq_rel);
    if (woke) {
        bsp_display_backlight((uint8_t)atomic_load_explicit(
            &s_values[PASSPORT_SETTING_BRIGHTNESS], memory_order_relaxed));
    }
    return woke;
}

void passport_settings_key_feedback(void)
{
    if (!s_initialized || !s_worker ||
        atomic_load_explicit(&s_values[PASSPORT_SETTING_KEY_SOUND],
                             memory_order_relaxed) == 0U) {
        return;
    }
    xTaskNotify(s_worker, SETTINGS_NOTIFY_SOUND, eSetBits);
}

void passport_settings_sound_preview(void)
{
    if (s_initialized && s_worker) {
        xTaskNotify(s_worker, SETTINGS_NOTIFY_PREVIEW, eSetBits);
    }
}
