#include "plugin_host.h"

#include "bsp_audio.h"
#include "device_settings.h"
#include "nearby_service.h"
#include "system_plugins.h"
#include "ui_pixel.h"
#include "ui_theme.h"
#include "lvgl.h"
#include "nvs.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define TONE_SAMPLE_RATE 16000U
#define TONE_QUEUE_DEPTH 4U
#define PLUGIN_KV_KEY_MAX 15U
#define PLUGIN_UI_OBJECT_MAX 24U
#define PLUGIN_UI_TEXT_MAX 128U
#define PLUGIN_KV_OPERATION_MAX 8U
#define PLUGIN_KV_KEY_COUNT_MAX 8U
#define PLUGIN_KV_WRITE_MAX 128U
#define PLUGIN_TIMER_MIN_MS 100U
#define PLUGIN_SEMANTIC_ROW_MAX 8U

typedef struct {
    const char *icon;
    const char *label;
    const char *text_value;
    int32_t value;
    plugin_ui_value_kind_t kind;
    uint8_t id;
    bool used;
    bool selected;
    bool enabled;
} semantic_row_t;

typedef struct {
    const char *title;
    const char *card_label;
    const char *card_suffix;
    const char *navigation_action;
    const char *ok_action;
    const char *back_action;
    semantic_row_t rows[PLUGIN_SEMANTIC_ROW_MAX];
    int32_t card_value;
    plugin_ui_value_kind_t card_kind;
    bool active;
    bool has_card;
} semantic_scene_t;

typedef struct {
    uint16_t frequency;
    uint16_t duration_ms;
    uint32_t generation;
} tone_work_t;

typedef struct host_state host_state_t;

typedef struct {
    host_state_t *host;
    uint8_t id;
    bool repeat;
} timer_context_t;

struct host_state {
    plugin_image_t image;
    plugin_vm_t vm;
    lv_obj_t *screen;
    lv_obj_t *action_bar;
    lv_obj_t *modal_action_bar;
    ui_pixel_dialog_t dialog;
    semantic_scene_t scene;
    lv_timer_t *timers[4];
    lv_timer_t *nearby_timer;
    timer_context_t timer_contexts[4];
    char nvs_namespace[16];
    uint8_t live_ui_objects;
    uint8_t kv_operations;
    uint16_t kv_writes;
    bool active;
    bool faulted;
    bool exit_requested;
    bool shell_screen;
    bool dialog_active;
    bool dialog_confirm_selected;
    uint16_t dialog_id;
    uint32_t owner;
    bool resources_claimed;
};

static const char *TAG = "plugin_host";
static host_state_t s_host;
static QueueHandle_t s_tone_queue;
static bool s_audio_available;
static _Atomic uint32_t s_tone_generation;
static uint32_t s_owner_serial;

static plugin_vm_result_t dispatch_event(plugin_event_t event, int32_t type,
                                         int32_t id, int32_t handle,
                                         int32_t value);

static bool make_nvs_namespace(char output[16], const char *plugin_id)
{
    static const char identity_prefix[] = "passport-kv-v4:";
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    char identity[sizeof(identity_prefix) + PLUGIN_ID_SIZE];
    uint8_t digest[32];

    int length = snprintf(identity, sizeof(identity), "%s%s", identity_prefix, plugin_id);
    if (length < 0 || (size_t)length >= sizeof(identity) ||
        mbedtls_sha256((const uint8_t *)identity, (size_t)length, digest, 0) != 0) {
        return false;
    }
    output[0] = 'q';
    for (size_t index = 0; index < 14U; ++index) {
        size_t bit = index * 5U;
        uint16_t window = (uint16_t)digest[bit / 8U] << 8 |
                          digest[bit / 8U + 1U];
        output[index + 1U] = alphabet[(window >> (11U - bit % 8U)) & 0x1fU];
    }
    output[15] = '\0';
    return true;
}

static const lv_font_t *font_for(uint8_t font)
{
    (void)font;
    return UI_FONT_BODY;
}

static uint32_t resolve_ui_color(uint32_t color)
{
    if ((color & PLUGIN_THEME_COLOR_REFERENCE_MASK) ==
        PLUGIN_THEME_COLOR_REFERENCE_FLAG) {
        uint8_t token = (uint8_t)color;
        if (token < PLUGIN_THEME_COLOR_COUNT) {
            return ui_theme_color((plugin_theme_color_t)token);
        }
        return ui_theme_color(PLUGIN_THEME_COLOR_TEXT);
    }
    return color & 0xffffffU;
}

static bool ui_object_allowed(host_state_t *host)
{
    if (host->live_ui_objects >= PLUGIN_UI_OBJECT_MAX) return false;
    ++host->live_ui_objects;
    return true;
}

static void format_value(char *output, size_t size, plugin_ui_value_kind_t kind,
                         const char *text, int32_t value, const char *suffix)
{
    if (!output || size == 0U) return;
    if (!text) text = "";
    if (!suffix) suffix = "";
    switch (kind) {
    case PLUGIN_UI_VALUE_NONE:
        output[0] = '\0';
        break;
    case PLUGIN_UI_VALUE_TEXT:
        snprintf(output, size, "%s", text);
        break;
    case PLUGIN_UI_VALUE_INTEGER:
        snprintf(output, size, "%ld%s", (long)value, suffix);
        break;
    case PLUGIN_UI_VALUE_PERCENT:
        snprintf(output, size, "%ld%%", (long)value);
        break;
    case PLUGIN_UI_VALUE_TOGGLE:
        snprintf(output, size, "%s", value ? "开启" : "关闭");
        break;
    case PLUGIN_UI_VALUE_DURATION:
        if (value == 0) snprintf(output, size, "%s", "从不");
        else if (value < 60) snprintf(output, size, "%ld 秒", (long)value);
        else snprintf(output, size, "%ld 分钟", (long)(value / 60));
        break;
    case PLUGIN_UI_VALUE_THEME:
        snprintf(output, size, "%s", value >= 0
            ? ui_theme_name((size_t)value) : "未知主题");
        break;
    default:
        output[0] = '\0';
        break;
    }
}

static void clear_modal(host_state_t *host)
{
    if (host->dialog.overlay) lv_obj_delete(host->dialog.overlay);
    if (host->modal_action_bar) lv_obj_delete(host->modal_action_bar);
    memset(&host->dialog, 0, sizeof(host->dialog));
    host->modal_action_bar = NULL;
    host->dialog_active = false;
    host->dialog_confirm_selected = false;
    host->dialog_id = 0U;
}

static bool render_semantic_scene(host_state_t *host)
{
    semantic_scene_t *scene = &host->scene;
    if (!scene->active || !scene->title) return false;
    if (host->screen) lv_obj_delete(host->screen);
    host->screen = ui_pixel_screen_create(scene->title);
    if (!host->screen) return false;
    host->action_bar = NULL;
    host->modal_action_bar = NULL;
    memset(&host->dialog, 0, sizeof(host->dialog));
    host->dialog_active = false;
    host->shell_screen = false;
    host->live_ui_objects = 0U;

    if (scene->has_card) {
        char value[64];
        format_value(value, sizeof(value), scene->card_kind, NULL,
                     scene->card_value, scene->card_suffix);
        ui_pixel_value_card(host->screen, scene->card_label, value);
    }

    semantic_row_t *visible[PLUGIN_SEMANTIC_ROW_MAX];
    size_t count = 0U;
    size_t selected = 0U;
    for (size_t id = 0; id < PLUGIN_SEMANTIC_ROW_MAX; ++id) {
        if (!scene->rows[id].used) continue;
        if (scene->rows[id].selected) selected = count;
        visible[count++] = &scene->rows[id];
    }
    size_t window = count > 5U && selected >= 5U ? selected - 4U : 0U;
    if (count > 5U && window + 5U > count) window = count - 5U;
    for (size_t row = 0; row < 5U && window + row < count; ++row) {
        semantic_row_t *item = visible[window + row];
        lv_obj_t *value_label = NULL;
        lv_obj_t *panel = ui_pixel_row_create(
            host->screen, 10, 39 + (int)row * 48, 220, 39,
            item->icon, item->label, &value_label);
        char value[64];
        format_value(value, sizeof(value), item->kind, item->text_value,
                     item->value, "");
        lv_label_set_text(value_label, value);
        ui_pixel_set_selected(panel, item->selected, item->enabled);
    }
    if (scene->navigation_action || scene->ok_action || scene->back_action) {
        host->action_bar = ui_pixel_action_bar(
            host->screen,
            scene->navigation_action ? scene->navigation_action : "",
            scene->ok_action ? scene->ok_action : "",
            scene->back_action ? scene->back_action : "");
    }
    lv_screen_load(host->screen);
    return true;
}

static bool host_ui_clear(void *context, uint32_t color)
{
    host_state_t *host = context;

    clear_modal(host);
    memset(&host->scene, 0, sizeof(host->scene));
    host->action_bar = NULL;
    if (host->screen && host->shell_screen) {
        lv_obj_delete(host->screen);
        host->screen = NULL;
        host->shell_screen = false;
    }
    if (!host->screen) {
        host->screen = lv_obj_create(NULL);
        if (!host->screen) return false;
        lv_obj_remove_flag(host->screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(host->screen, 0, 0);
        lv_obj_set_style_pad_all(host->screen, 0, 0);
        lv_screen_load(host->screen);
    } else {
        lv_obj_clean(host->screen);
    }
    host->live_ui_objects = 0;
    lv_obj_set_style_bg_color(host->screen, lv_color_hex(resolve_ui_color(color)), 0);
    return true;
}

static bool host_ui_title(void *context, const char *title)
{
    host_state_t *host = context;
    if (!host->screen || !title || strnlen(title, 49U) > 48U ||
        !ui_object_allowed(host)) {
        return false;
    }
    return ui_pixel_status_bar(host->screen, title) != NULL;
}

static bool set_text_geometry(lv_obj_t *label, int16_t x, int16_t y,
                              plugin_vm_align_t alignment)
{
    if (alignment > PLUGIN_VM_ALIGN_RIGHT || y < -40 || y > 320) return false;
    lv_obj_set_y(label, y);
    if (alignment == PLUGIN_VM_ALIGN_LEFT) {
        if (x < -40 || x > 240) return false;
        lv_obj_set_x(label, x);
        lv_obj_set_width(label, 240 - x);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    } else if (alignment == PLUGIN_VM_ALIGN_CENTER) {
        lv_obj_set_x(label, x - 120);
        lv_obj_set_width(label, 240);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        if (x < 0 || x > 280) return false;
        lv_obj_set_x(label, x - 240);
        lv_obj_set_width(label, 240);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    }
    return true;
}

static bool host_ui_text(void *context, int16_t x, int16_t y, uint8_t font,
                         plugin_vm_align_t alignment, uint32_t color, const char *text)
{
    host_state_t *host = context;
    if (!host->screen || !text || font > 1U ||
        strnlen(text, PLUGIN_UI_TEXT_MAX + 1U) > PLUGIN_UI_TEXT_MAX ||
        !ui_object_allowed(host)) {
        return false;
    }

    lv_obj_t *label = lv_label_create(host->screen);
    if (!label) return false;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font_for(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(resolve_ui_color(color)), 0);
    if (!set_text_geometry(label, x, y, alignment)) {
        lv_obj_delete(label);
        return false;
    }
    return true;
}

static bool host_ui_state(void *context, int16_t x, int16_t y, uint8_t font,
                          plugin_vm_align_t alignment, uint32_t color,
                          const char *prefix, int32_t value)
{
    char text[96];
    if (!prefix || strnlen(prefix, 65U) > 64U) return false;
    int length = snprintf(text, sizeof(text), "%s%ld", prefix, (long)value);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        return false;
    }
    return host_ui_text(context, x, y, font, alignment, color, text);
}

static bool host_ui_rect(void *context, int16_t x, int16_t y, int16_t width,
                         int16_t height, uint32_t color)
{
    host_state_t *host = context;
    if (!host->screen || x < -240 || y < -320 || width <= 0 || height <= 0 ||
        width > 480 || height > 640) {
        return false;
    }
    if (!ui_object_allowed(host)) return false;
    lv_obj_t *rectangle = lv_obj_create(host->screen);
    if (!rectangle) return false;
    lv_obj_remove_flag(rectangle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(rectangle, x, y);
    lv_obj_set_size(rectangle, width, height);
    lv_obj_set_style_radius(rectangle, 0, 0);
    lv_obj_set_style_border_width(rectangle, 0, 0);
    lv_obj_set_style_pad_all(rectangle, 0, 0);
    lv_obj_set_style_bg_color(rectangle, lv_color_hex(resolve_ui_color(color)), 0);
    return true;
}

static bool host_ui_commit(void *context)
{
    host_state_t *host = context;
    if (host->scene.active) return render_semantic_scene(host);
    if (!host->screen) return false;
    lv_obj_invalidate(host->screen);
    return true;
}

static bool host_ui_screen(void *context, const char *title)
{
    host_state_t *host = context;
    if (!title || strnlen(title, 49U) > 48U) return false;
    memset(&host->scene, 0, sizeof(host->scene));
    host->scene.active = true;
    host->scene.title = title;
    return true;
}

static bool host_ui_value_card(void *context, const char *label,
                               plugin_ui_value_kind_t kind, int32_t value,
                               const char *suffix)
{
    host_state_t *host = context;
    if (!host->scene.active || host->scene.has_card || !label || !suffix ||
        kind <= PLUGIN_UI_VALUE_TEXT || kind >= PLUGIN_UI_VALUE_COUNT ||
        strnlen(label, 49U) > 48U || strnlen(suffix, 25U) > 24U) {
        return false;
    }
    host->scene.has_card = true;
    host->scene.card_label = label;
    host->scene.card_suffix = suffix;
    host->scene.card_kind = kind;
    host->scene.card_value = value;
    return true;
}

static bool host_ui_list_row(void *context, uint8_t row_id, const char *icon,
                             const char *label, plugin_ui_value_kind_t kind,
                             const char *text_value, int32_t value, bool selected,
                             bool enabled)
{
    host_state_t *host = context;
    if (!host->scene.active || row_id >= PLUGIN_SEMANTIC_ROW_MAX || !icon ||
        !label || !text_value || kind >= PLUGIN_UI_VALUE_COUNT ||
        strnlen(icon, 17U) > 16U || strnlen(label, 49U) > 48U ||
        strnlen(text_value, 49U) > 48U) {
        return false;
    }
    host->scene.rows[row_id] = (semantic_row_t) {
        .icon = icon,
        .label = label,
        .text_value = text_value,
        .value = value,
        .kind = kind,
        .id = row_id,
        .used = true,
        .selected = selected,
        .enabled = enabled,
    };
    return true;
}

static bool host_ui_action_bar(void *context, const char *navigation,
                               const char *ok, const char *back)
{
    host_state_t *host = context;
    if (!navigation || !ok || !back || strnlen(navigation, 25U) > 24U ||
        strnlen(ok, 25U) > 24U || strnlen(back, 25U) > 24U) {
        return false;
    }
    if (host->scene.active) {
        host->scene.navigation_action = navigation;
        host->scene.ok_action = ok;
        host->scene.back_action = back;
        return true;
    }
    if (!host->screen) return false;
    if (host->action_bar) lv_obj_delete(host->action_bar);
    host->action_bar = ui_pixel_action_bar(host->screen, navigation, ok, back);
    return host->action_bar != NULL;
}

static bool host_ui_dialog_confirm(void *context, uint16_t dialog_id,
                                   const char *title, const char *message,
                                   const char *cancel, const char *confirm)
{
    host_state_t *host = context;
    if (!host->screen || host->dialog_active || dialog_id == 0U || !title ||
        !message || !cancel || !confirm || strnlen(title, 49U) > 48U ||
        strnlen(message, 97U) > 96U || strnlen(cancel, 25U) > 24U ||
        strnlen(confirm, 25U) > 24U ||
        !ui_pixel_dialog_create(&host->dialog, host->screen)) {
        return false;
    }
    host->dialog_active = true;
    host->dialog_confirm_selected = false;
    host->dialog_id = dialog_id;
    ui_pixel_dialog_show_confirm(&host->dialog, title, message, cancel, confirm, false);
    host->modal_action_bar = ui_pixel_action_bar(
        host->screen, "选择", "确认", "取消");
    if (!host->modal_action_bar) {
        clear_modal(host);
        return false;
    }
    lv_obj_move_foreground(host->modal_action_bar);
    return true;
}

static bool host_theme_next(void *context, int32_t *index)
{
    (void)context;
    return ui_theme_select_next(index) == ESP_OK;
}

static bool host_theme_color(void *context, uint8_t token, int32_t *color)
{
    (void)context;
    if (!color || token >= PLUGIN_THEME_COLOR_COUNT) return false;
    *color = (int32_t)ui_theme_color((plugin_theme_color_t)token);
    return true;
}

static bool host_device_info(void *context)
{
    host_state_t *host = context;
    clear_modal(host);
    memset(&host->scene, 0, sizeof(host->scene));
    if (host->screen) lv_obj_delete(host->screen);
    host->screen = system_device_info_screen_create();
    if (!host->screen) return false;
    host->live_ui_objects = 0U;
    host->action_bar = NULL;
    host->shell_screen = true;
    lv_screen_load(host->screen);
    return true;
}

static bool host_tone(void *context, uint16_t frequency, uint16_t duration_ms)
{
    (void)context;
    tone_work_t work = {
        .frequency = frequency,
        .duration_ms = duration_ms,
        .generation = atomic_load_explicit(&s_tone_generation, memory_order_acquire),
    };
    if (!s_audio_available || !s_tone_queue || frequency < 20U || frequency > 10000U ||
        duration_ms == 0U || duration_ms > 1000U) {
        return false;
    }
    return xQueueSend(s_tone_queue, &work, 0) == pdTRUE;
}

static bool kv_key_valid(const char *key)
{
    if (!key) return false;
    size_t size = strnlen(key, PLUGIN_KV_KEY_MAX + 1U);
    if (size == 0U || size > PLUGIN_KV_KEY_MAX) return false;
    for (size_t index = 0; index < size; ++index) {
        unsigned char value = (unsigned char)key[index];
        if (value < 0x21U || value > 0x7eU) return false;
    }
    return true;
}

static bool host_kv_load(void *context, const char *key, int32_t fallback, int32_t *value)
{
    host_state_t *host = context;
    nvs_handle_t handle;
    if (!value || !kv_key_valid(key) ||
        host->kv_operations >= PLUGIN_KV_OPERATION_MAX) {
        return false;
    }
    ++host->kv_operations;
    *value = fallback;
    esp_err_t result = nvs_open(host->nvs_namespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return true;
    if (result != ESP_OK) return false;
    result = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

static bool host_kv_save(void *context, const char *key, int32_t value)
{
    host_state_t *host = context;
    nvs_handle_t handle;
    int32_t existing_value;
    size_t used_entries;

    if (!kv_key_valid(key) || host->kv_operations >= PLUGIN_KV_OPERATION_MAX) {
        return false;
    }
    ++host->kv_operations;
    if (nvs_open(host->nvs_namespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_get_i32(handle, key, &existing_value);
    if (result == ESP_OK && existing_value == value) {
        nvs_close(handle);
        return true;
    }
    if (host->kv_writes >= PLUGIN_KV_WRITE_MAX) {
        nvs_close(handle);
        return false;
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = nvs_get_used_entry_count(handle, &used_entries);
        if (result == ESP_OK && used_entries >= PLUGIN_KV_KEY_COUNT_MAX) {
            result = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
        }
    }
    if (result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND) {
        result = nvs_set_i32(handle, key, value);
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) ++host->kv_writes;
    return result == ESP_OK;
}

static void timer_callback(lv_timer_t *timer)
{
    timer_context_t *timer_context = lv_timer_get_user_data(timer);
    host_state_t *host = timer_context->host;
    if (!timer_context->repeat) host->timers[timer_context->id] = NULL;
    plugin_vm_result_t result = plugin_host_dispatch(
        (plugin_event_t)(PLUGIN_EVENT_TIMER0 + timer_context->id));
    if (result != PLUGIN_VM_OK && result != PLUGIN_VM_NO_HANDLER) {
        ESP_LOGE(TAG, "timer handler failed: %s", plugin_vm_result_name(result));
    }
}

static bool host_timer_set(void *context, uint8_t timer_id, uint32_t delay_ms, bool repeat)
{
    host_state_t *host = context;
    if (timer_id >= 4U || delay_ms < PLUGIN_TIMER_MIN_MS || delay_ms > 3600000U) {
        return false;
    }
    if (host->timers[timer_id]) {
        lv_timer_delete(host->timers[timer_id]);
        host->timers[timer_id] = NULL;
    }
    timer_context_t *timer_context = &host->timer_contexts[timer_id];
    *timer_context = (timer_context_t) {
        .host = host,
        .id = timer_id,
        .repeat = repeat,
    };
    host->timers[timer_id] = lv_timer_create(timer_callback, delay_ms, timer_context);
    if (!host->timers[timer_id]) return false;
    if (!repeat) lv_timer_set_repeat_count(host->timers[timer_id], 1);
    return true;
}

static void host_request_exit(void *context)
{
    host_state_t *host = context;
    host->exit_requested = true;
}

static bool host_setting_get(void *context, uint8_t setting_id, int32_t *value)
{
    (void)context;
    if (setting_id == PLUGIN_SETTING_THEME) {
        if (!value) return false;
        *value = (int32_t)ui_theme_active_index();
        return true;
    }
    return device_settings_get(setting_id, value);
}

static bool host_setting_set(void *context, uint8_t setting_id, int32_t value)
{
    (void)context;
    if (setting_id == PLUGIN_SETTING_THEME) {
        return value >= 0 && ui_theme_select((size_t)value) == ESP_OK;
    }
    return device_settings_set(setting_id, value);
}

static bool host_buffer_alloc(void *context, uint16_t capacity, int32_t *handle)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_alloc(host->owner, capacity, handle);
}

static bool host_buffer_release(void *context, int32_t handle)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_release(host->owner, handle);
}

static bool host_buffer_length(void *context, int32_t handle, int32_t *length)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_length(host->owner, handle, length);
}

static bool host_buffer_read_u8(void *context, int32_t handle, int32_t index,
                                int32_t *value)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_read_u8(host->owner, handle, index, value);
}

static bool host_buffer_write_u8(void *context, int32_t handle, int32_t index,
                                 int32_t value)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_write_u8(host->owner, handle, index, value);
}

static bool host_buffer_append_text(void *context, int32_t handle,
                                    const char *text)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_buffer_append_text(host->owner, handle, text);
}

static bool host_nearby_acquire(void *context)
{
    host_state_t *host = context;
    return host->resources_claimed && nearby_service_acquire(host->owner);
}

static bool host_nearby_release(void *context)
{
    host_state_t *host = context;
    return host->resources_claimed && nearby_service_release(host->owner);
}

static bool host_nearby_send(void *context, int32_t handle, int32_t *message_id)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_send(host->owner, handle, message_id);
}

static bool host_nearby_blob_accept(void *context, int32_t transfer_id)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_blob_accept(host->owner, transfer_id);
}

static bool host_nearby_blob_reject(void *context, int32_t transfer_id)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_blob_reject(host->owner, transfer_id);
}

static bool host_nearby_blob_send(void *context, int32_t handle,
                                  const char *name, const char *mime,
                                  int32_t *transfer_id)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_blob_send(host->owner, handle, name, mime,
                                    transfer_id);
}

static bool host_nearby_voice_start(void *context)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_voice_start(host->owner);
}

static bool host_nearby_voice_transmit(void *context, bool enabled)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_voice_transmit(host->owner, enabled);
}

static bool host_nearby_voice_stop(void *context)
{
    host_state_t *host = context;
    return host->resources_claimed &&
           nearby_service_voice_stop(host->owner);
}

static void tone_task(void *argument)
{
    tone_work_t work;
    int16_t samples[128];
    (void)argument;

    for (;;) {
        if (xQueueReceive(s_tone_queue, &work, portMAX_DELAY) != pdTRUE) continue;
        if (work.generation != atomic_load_explicit(&s_tone_generation, memory_order_acquire)) {
            continue;
        }
        if (!bsp_audio_session_begin(1000)) continue;
        if (work.generation != atomic_load_explicit(&s_tone_generation, memory_order_acquire)) {
            bsp_audio_session_end();
            continue;
        }
        if (bsp_audio_set_format(TONE_SAMPLE_RATE, 16, 1) != ESP_OK) {
            bsp_audio_session_end();
            continue;
        }
        int32_t volume = 55;
        device_settings_get(PLUGIN_SETTING_VOLUME, &volume);
        bsp_audio_set_volume((uint8_t)volume);
        uint32_t phase = 0;
        uint32_t remaining = TONE_SAMPLE_RATE * work.duration_ms / 1000U;
        while (remaining > 0U &&
               work.generation == atomic_load_explicit(&s_tone_generation,
                                                       memory_order_acquire)) {
            size_t count = remaining > 128U ? 128U : remaining;
            for (size_t index = 0; index < count; ++index) {
                phase += work.frequency;
                if (phase >= TONE_SAMPLE_RATE) phase -= TONE_SAMPLE_RATE;
                samples[index] = phase < TONE_SAMPLE_RATE / 2U ? 2600 : -2600;
            }
            if (bsp_audio_write(samples, count * sizeof(samples[0])) != ESP_OK) break;
            remaining -= count;
        }
        bsp_audio_session_end();
    }
}

static void stop_timers(host_state_t *host)
{
    for (size_t index = 0; index < 4U; ++index) {
        if (host->timers[index]) {
            lv_timer_delete(host->timers[index]);
            host->timers[index] = NULL;
        }
    }
}

static void render_fault(plugin_vm_result_t result)
{
    char message[96];
    atomic_fetch_add_explicit(&s_tone_generation, 1U, memory_order_acq_rel);
    stop_timers(&s_host);
    if (s_host.nearby_timer) {
        lv_timer_delete(s_host.nearby_timer);
        s_host.nearby_timer = NULL;
    }
    if (s_host.resources_claimed) {
        nearby_service_foreground_exit(s_host.owner);
        s_host.resources_claimed = false;
    }
    s_host.faulted = true;
    host_ui_clear(&s_host, UI_SKY);
    host_ui_title(&s_host, "插件错误");
    snprintf(message, sizeof(message), "错误：\n%s", plugin_vm_result_name(result));
    host_ui_text(&s_host, 12, 100, 0, PLUGIN_VM_ALIGN_LEFT, UI_DANGER, message);
    host_ui_text(&s_host, 12, 190, 0, PLUGIN_VM_ALIGN_LEFT, UI_INK,
                 "长按 OK 返回");
}

static void nearby_timer_callback(lv_timer_t *timer)
{
    host_state_t *host = lv_timer_get_user_data(timer);
    nearby_event_t event;

    if (!host || !host->active || host->faulted || !host->resources_claimed) {
        return;
    }
    for (size_t count = 0; count < 4U; ++count) {
        if (!nearby_service_poll(host->owner, &event)) return;
        plugin_vm_result_t result = dispatch_event(
            PLUGIN_EVENT_NEARBY, event.type, event.id, event.handle, event.value);
        if (result != PLUGIN_VM_OK && result != PLUGIN_VM_NO_HANDLER) return;
    }
}

static esp_err_t ensure_tone_worker(void)
{
    if (!s_audio_available) return ESP_ERR_NOT_SUPPORTED;
    if (s_tone_queue) return ESP_OK;
    s_tone_queue = xQueueCreate(TONE_QUEUE_DEPTH, sizeof(tone_work_t));
    if (!s_tone_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(tone_task, "plugin_tone", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_tone_queue);
        s_tone_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t plugin_host_system_init(bool audio_available)
{
    s_audio_available = audio_available;
    return ESP_OK;
}

esp_err_t plugin_host_start(const plugin_record_t *record)
{
    plugin_vm_host_t callbacks = {
        .context = &s_host,
        .ui_clear = host_ui_clear,
        .ui_title = host_ui_title,
        .ui_text = host_ui_text,
        .ui_state = host_ui_state,
        .ui_rect = host_ui_rect,
        .ui_commit = host_ui_commit,
        .ui_screen = host_ui_screen,
        .ui_value_card = host_ui_value_card,
        .ui_list_row = host_ui_list_row,
        .ui_action_bar = host_ui_action_bar,
        .ui_dialog_confirm = host_ui_dialog_confirm,
        .theme_next = host_theme_next,
        .theme_color = host_theme_color,
        .device_info = host_device_info,
        .tone = host_tone,
        .kv_load = host_kv_load,
        .kv_save = host_kv_save,
        .timer_set = host_timer_set,
        .setting_get = host_setting_get,
        .setting_set = host_setting_set,
        .buffer_alloc = host_buffer_alloc,
        .buffer_release = host_buffer_release,
        .buffer_length = host_buffer_length,
        .buffer_read_u8 = host_buffer_read_u8,
        .buffer_write_u8 = host_buffer_write_u8,
        .buffer_append_text = host_buffer_append_text,
        .nearby_acquire = host_nearby_acquire,
        .nearby_release = host_nearby_release,
        .nearby_send = host_nearby_send,
        .nearby_blob_accept = host_nearby_blob_accept,
        .nearby_blob_reject = host_nearby_blob_reject,
        .nearby_blob_send = host_nearby_blob_send,
        .nearby_voice_start = host_nearby_voice_start,
        .nearby_voice_transmit = host_nearby_voice_transmit,
        .nearby_voice_stop = host_nearby_voice_stop,
        .request_exit = host_request_exit,
    };

    if (!record || s_host.active) return ESP_ERR_INVALID_STATE;
    if (record->manifest.kind != PLUGIN_KIND_APP) return ESP_ERR_INVALID_ARG;
    if ((record->manifest.permissions & PLUGIN_PERMISSION_AUDIO) != 0U) {
        esp_err_t tone_result = ensure_tone_worker();
        if (tone_result != ESP_OK) return tone_result;
    }
    atomic_fetch_add_explicit(&s_tone_generation, 1U, memory_order_acq_rel);
    memset(&s_host, 0, sizeof(s_host));
    s_owner_serial = s_owner_serial == UINT32_MAX ? 1U : s_owner_serial + 1U;
    if (s_owner_serial == 0U) s_owner_serial = 1U;
    s_host.owner = s_owner_serial;
    esp_err_t result = plugin_store_open(record, &s_host.image);
    if (result != ESP_OK) return result;
    if (!make_nvs_namespace(s_host.nvs_namespace, record->manifest.id)) {
        plugin_store_close(&s_host.image);
        return ESP_FAIL;
    }
    plugin_vm_result_t vm_result = plugin_vm_init(
        &s_host.vm, s_host.image.content,
        record->package_size - PLUGIN_PACKAGE_HEADER_SIZE, &callbacks);
    if (vm_result != PLUGIN_VM_OK) {
        plugin_store_close(&s_host.image);
        return ESP_ERR_INVALID_ARG;
    }
    result = nearby_service_foreground_enter(s_host.owner);
    if (result != ESP_OK) {
        plugin_store_close(&s_host.image);
        memset(&s_host, 0, sizeof(s_host));
        return result;
    }
    s_host.resources_claimed = true;
    s_host.active = true;
    s_host.nearby_timer = lv_timer_create(nearby_timer_callback, 20U, &s_host);
    if (!s_host.nearby_timer) {
        plugin_host_stop();
        return ESP_ERR_NO_MEM;
    }
    host_ui_clear(&s_host, UI_SKY);
    host_ui_title(&s_host, record->manifest.name);
    vm_result = plugin_host_dispatch(PLUGIN_EVENT_START);
    if (vm_result != PLUGIN_VM_OK && vm_result != PLUGIN_VM_NO_HANDLER) {
        plugin_host_stop();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "started %s", record->manifest.id);
    return ESP_OK;
}

void plugin_host_stop(void)
{
    if (!s_host.active) return;
    atomic_fetch_add_explicit(&s_tone_generation, 1U, memory_order_acq_rel);
    stop_timers(&s_host);
    if (s_host.nearby_timer) {
        lv_timer_delete(s_host.nearby_timer);
        s_host.nearby_timer = NULL;
    }
    if (s_host.resources_claimed) {
        nearby_service_foreground_exit(s_host.owner);
        s_host.resources_claimed = false;
    }
    if (s_host.screen) lv_obj_delete(s_host.screen);
    plugin_store_close(&s_host.image);
    memset(&s_host, 0, sizeof(s_host));
}

static plugin_vm_result_t dispatch_event(plugin_event_t event, int32_t type,
                                         int32_t id, int32_t handle,
                                         int32_t value)
{
    if (!s_host.active) return PLUGIN_VM_INVALID_ARGUMENT;
    if (s_host.faulted) {
        return event == PLUGIN_EVENT_BACK ? PLUGIN_VM_NO_HANDLER :
                                            PLUGIN_VM_HOST_ERROR;
    }
    s_host.kv_operations = 0;
    plugin_vm_result_t result = plugin_vm_dispatch_event(
        &s_host.vm, event, type, id, handle, value);
    if (result != PLUGIN_VM_OK && result != PLUGIN_VM_NO_HANDLER) render_fault(result);
    return result;
}

plugin_vm_result_t plugin_host_dispatch(plugin_event_t event)
{
    return dispatch_event(event, 0, 0, 0, 0);
}

bool plugin_host_handle_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_host.active || !s_host.dialog_active) return false;
    if (event == BSP_BTN_PRESS &&
        (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        s_host.dialog_confirm_selected = !s_host.dialog_confirm_selected;
        ui_pixel_dialog_set_selected(&s_host.dialog,
                                     s_host.dialog_confirm_selected);
        return true;
    }
    if (button == BSP_BTN_OK &&
        (event == BSP_BTN_CLICK || event == BSP_BTN_LONG)) {
        uint16_t dialog_id = s_host.dialog_id;
        int32_t value = event == BSP_BTN_CLICK && s_host.dialog_confirm_selected;
        clear_modal(&s_host);
        dispatch_event(PLUGIN_EVENT_ACTION, 0, dialog_id, 0, value);
        return true;
    }
    return true;
}

bool plugin_host_take_exit_request(void)
{
    bool requested = s_host.exit_requested;
    s_host.exit_requested = false;
    return requested;
}
