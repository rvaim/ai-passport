#include "passport_runtime_ui.h"

#include "passport_package.h"
#include "passport_storage.h"
#include "passport_theme.h"
#include "lauxlib.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_HANDLE_METATABLE "passport.ui.handle"
#define PAP_UI_OBJECT_LIMIT 48U
#define PAP_UI_BUFFER_LIMIT (32U * 1024U)
#define PAP_UI_LINE_POINTS_MAX 64U
#define PAP_UI_PATH_MAX 120U

struct passport_runtime_ui_resource {
    passport_runtime_ui_resource_t *next;
    size_t size;
    unsigned char data[];
};

typedef struct {
    lv_obj_t *object;
    uint32_t generation;
    uint8_t style;
    uint8_t kind;
} ui_handle_t;

typedef struct {
    const char *name;
    lua_Integer value;
} lua_enum_value_t;

static passport_runtime_ui_state_t *s_ui;

static int32_t check_int32(lua_State *L, int argument)
{
    lua_Integer value = luaL_checkinteger(L, argument);
    if (value < INT32_MIN || value > INT32_MAX) {
        luaL_argerror(L, argument, "整数超出 32 位范围");
    }
    return (int32_t)value;
}

static passport_style_id_t check_style(lua_State *L, int argument,
                                       passport_style_id_t fallback)
{
    if (lua_isnoneornil(L, argument)) return fallback;
    lua_Integer value = luaL_checkinteger(L, argument);
    if (value < 0 || value >= PASSPORT_STYLE_COUNT) {
        luaL_argerror(L, argument, "无效样式枚举");
    }
    return (passport_style_id_t)value;
}

static ui_handle_t *check_handle(lua_State *L, int argument)
{
    ui_handle_t *handle = luaL_checkudata(L, argument, UI_HANDLE_METATABLE);
    if (!s_ui || !handle || handle->generation != s_ui->generation ||
        !handle->object || !lv_obj_is_valid(handle->object)) {
        luaL_argerror(L, argument, "对象已随页面销毁");
    }
    return handle;
}

static ui_handle_t *check_kind(lua_State *L, int argument,
                               passport_ui_object_kind_t kind)
{
    ui_handle_t *handle = check_handle(L, argument);
    if (handle->kind != kind) luaL_argerror(L, argument, "组件类型不匹配");
    return handle;
}

static lv_obj_t *optional_parent(lua_State *L, int argument)
{
    if (lua_isnoneornil(L, argument)) return NULL;
    return check_kind(L, argument, PASSPORT_UI_OBJECT_VIEW)->object;
}

static void check_object_capacity(lua_State *L, uint8_t count)
{
    if (!s_ui || !s_ui->page) luaL_error(L, "当前没有页面");
    if ((unsigned)s_ui->object_count + count > PAP_UI_OBJECT_LIMIT) {
        luaL_error(L, "页面 LVGL 对象数量已达上限");
    }
}

static void reserve_objects(lua_State *L, uint8_t count)
{
    check_object_capacity(L, count);
    s_ui->object_count += count;
}

static int push_handle(lua_State *L, lv_obj_t *object,
                       passport_style_id_t style,
                       passport_ui_object_kind_t kind)
{
    if (!object) return luaL_error(L, "组件创建失败");
    ui_handle_t *handle = lua_newuserdatauv(L, sizeof(*handle), 0);
    *handle = (ui_handle_t) {
        .object = object,
        .generation = s_ui->generation,
        .style = (uint8_t)style,
        .kind = (uint8_t)kind,
    };
    luaL_setmetatable(L, UI_HANDLE_METATABLE);
    return 1;
}

static int create_simple(lua_State *L, passport_ui_object_kind_t kind,
                         const char *text, passport_style_id_t style,
                         lv_obj_t *parent, uint8_t object_cost)
{
    reserve_objects(L, object_cost);
    lv_obj_t *object = passport_ui_object_create(
        s_ui->page, parent, kind, text, style);
    if (!object) {
        s_ui->object_count -= object_cost;
        return luaL_error(L, "组件创建失败");
    }
    return push_handle(L, object, style, kind);
}

static passport_runtime_ui_resource_t *allocate_resource(lua_State *L,
                                                         size_t size)
{
    if (size == 0U || size > PAP_UI_BUFFER_LIMIT ||
        s_ui->buffer_used > PAP_UI_BUFFER_LIMIT - size) {
        luaL_error(L, "页面图片、Line 与 Canvas 缓冲总量超过 32 KiB");
    }
    passport_runtime_ui_resource_t *resource = malloc(sizeof(*resource) + size);
    if (!resource) luaL_error(L, "组件缓冲分配失败");
    resource->next = NULL;
    resource->size = size;
    return resource;
}

static void keep_resource(passport_runtime_ui_resource_t *resource)
{
    resource->next = s_ui->resources;
    s_ui->resources = resource;
    s_ui->buffer_used += resource->size;
}

static int l_ui_view(lua_State *L)
{
    passport_style_id_t style = check_style(L, 1, PASSPORT_STYLE_VIEW);
    return create_simple(L, PASSPORT_UI_OBJECT_VIEW, NULL, style,
                         optional_parent(L, 2), 1);
}

static int l_ui_text(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_TEXT);
    return create_simple(L, PASSPORT_UI_OBJECT_TEXT, text, style,
                         optional_parent(L, 3), 1);
}

static int l_ui_button(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_BUTTON);
    return create_simple(L, PASSPORT_UI_OBJECT_BUTTON, text, style,
                         optional_parent(L, 3), 2);
}

static int l_ui_list(lua_State *L)
{
    passport_style_id_t style = check_style(L, 1, PASSPORT_STYLE_LIST);
    return create_simple(L, PASSPORT_UI_OBJECT_LIST, NULL, style,
                         optional_parent(L, 2), 1);
}

static int l_ui_list_item(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    lv_obj_t *list = check_kind(L, 2, PASSPORT_UI_OBJECT_LIST)->object;
    passport_style_id_t style = check_style(L, 3, PASSPORT_STYLE_LIST_ITEM);
    return create_simple(L, PASSPORT_UI_OBJECT_LIST_ITEM, text, style, list, 2);
}

static int l_ui_bar(lua_State *L)
{
    int32_t value = check_int32(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_BAR);
    int result = create_simple(L, PASSPORT_UI_OBJECT_BAR, NULL, style,
                               optional_parent(L, 3), 1);
    ui_handle_t *handle = lua_touserdata(L, -1);
    passport_ui_object_set_value(handle->object, PASSPORT_UI_OBJECT_BAR,
                                 value, false);
    return result;
}

static int l_ui_arc(lua_State *L)
{
    int32_t value = check_int32(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_ARC);
    int result = create_simple(L, PASSPORT_UI_OBJECT_ARC, NULL, style,
                               optional_parent(L, 3), 1);
    ui_handle_t *handle = lua_touserdata(L, -1);
    passport_ui_object_set_value(handle->object, PASSPORT_UI_OBJECT_ARC,
                                 value, false);
    return result;
}

static int l_ui_slider(lua_State *L)
{
    int32_t value = check_int32(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_SLIDER);
    int result = create_simple(L, PASSPORT_UI_OBJECT_SLIDER, NULL, style,
                               optional_parent(L, 3), 1);
    ui_handle_t *handle = lua_touserdata(L, -1);
    passport_ui_object_set_value(handle->object, PASSPORT_UI_OBJECT_SLIDER,
                                 value, false);
    return result;
}

static int l_ui_switch(lua_State *L)
{
    bool checked = lua_toboolean(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_SWITCH);
    int result = create_simple(L, PASSPORT_UI_OBJECT_SWITCH, NULL, style,
                               optional_parent(L, 3), 1);
    ui_handle_t *handle = lua_touserdata(L, -1);
    passport_ui_object_set_checked(handle->object, PASSPORT_UI_OBJECT_SWITCH,
                                   checked);
    return result;
}

static int l_ui_spinner(lua_State *L)
{
    passport_style_id_t style = check_style(L, 1, PASSPORT_STYLE_SPINNER);
    return create_simple(L, PASSPORT_UI_OBJECT_SPINNER, NULL, style,
                         optional_parent(L, 2), 1);
}

static int l_ui_checkbox(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    bool checked = lua_toboolean(L, 2);
    passport_style_id_t style = check_style(L, 3, PASSPORT_STYLE_CHECKBOX);
    int result = create_simple(L, PASSPORT_UI_OBJECT_CHECKBOX, text, style,
                               optional_parent(L, 4), 1);
    ui_handle_t *handle = lua_touserdata(L, -1);
    passport_ui_object_set_checked(handle->object, PASSPORT_UI_OBJECT_CHECKBOX,
                                   checked);
    return result;
}

static int l_ui_line(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    size_t values = lua_rawlen(L, 1);
    if (values < 4U || (values & 1U) != 0U ||
        values / 2U > PAP_UI_LINE_POINTS_MAX) {
        return luaL_argerror(L, 1, "Line 需要 2..64 个 {x1,y1,x2,y2,...} 点");
    }
    size_t count = values / 2U;
    lv_point_precise_t parsed[PAP_UI_LINE_POINTS_MAX];
    int32_t max_x = 0;
    int32_t max_y = 0;
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, (lua_Integer)(i * 2U + 1U));
        lua_rawgeti(L, 1, (lua_Integer)(i * 2U + 2U));
        lua_Integer x = luaL_checkinteger(L, -2);
        lua_Integer y = luaL_checkinteger(L, -1);
        lua_pop(L, 2);
        if (x < 0 || x >= 240 || y < 0 || y >= 320) {
            return luaL_argerror(L, 1, "Line 坐标超出屏幕范围");
        }
        parsed[i] = (lv_point_precise_t) {.x = (lv_value_precise_t)x,
                                          .y = (lv_value_precise_t)y};
        if (x > max_x) max_x = (int32_t)x;
        if (y > max_y) max_y = (int32_t)y;
    }
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_LINE);
    lv_obj_t *parent = optional_parent(L, 3);
    size_t bytes = count * sizeof(lv_point_precise_t);
    check_object_capacity(L, 1);
    passport_runtime_ui_resource_t *resource = allocate_resource(L, bytes);
    lv_point_precise_t *points = (lv_point_precise_t *)resource->data;
    memcpy(points, parsed, bytes);
    ++s_ui->object_count;
    lv_obj_t *line = passport_ui_object_create(
        s_ui->page, parent, PASSPORT_UI_OBJECT_LINE, NULL, style);
    if (!line) {
        --s_ui->object_count;
        free(resource);
        return luaL_error(L, "Line 创建失败");
    }
    lv_line_set_points(line, points, (uint32_t)count);
    lv_obj_set_size(line, max_x + 1, max_y + 1);
    keep_resource(resource);
    return push_handle(L, line, style, PASSPORT_UI_OBJECT_LINE);
}

static int l_ui_image(lua_State *L)
{
    size_t path_length = 0;
    const char *relative = luaL_checklstring(L, 1, &path_length);
    int width = check_int32(L, 2);
    int height = check_int32(L, 3);
    if (path_length == 0U || path_length >= PAP_UI_PATH_MAX ||
        !passport_package_path_is_safe(relative)) {
        return luaL_argerror(L, 1, "图片必须使用 PAP 内安全的相对路径");
    }
    if (width <= 0 || width > 240 || height <= 0 || height > 320) {
        return luaL_error(L, "图片尺寸超出屏幕范围");
    }
    passport_style_id_t style = check_style(L, 4, PASSPORT_STYLE_IMAGE);
    lv_obj_t *parent = optional_parent(L, 5);
    size_t pixel_bytes = (size_t)width * (size_t)height * sizeof(uint16_t);
    check_object_capacity(L, 1);
    passport_runtime_ui_resource_t *resource = allocate_resource(
        L, sizeof(lv_image_dsc_t) + pixel_bytes);
    lv_image_dsc_t *image = (lv_image_dsc_t *)resource->data;
    unsigned char *pixels = resource->data + sizeof(*image);
    char path[288];
    if (!s_ui->app_root || snprintf(path, sizeof(path), "%s/%s",
                                    s_ui->app_root, relative) >= (int)sizeof(path)) {
        free(resource);
        return luaL_error(L, "图片路径过长");
    }
    if (!passport_storage_lock(UINT32_MAX)) {
        free(resource);
        return luaL_error(L, "插件存储正忙");
    }
    FILE *file = fopen(path, "rb");
    if (!file || fread(pixels, 1, pixel_bytes, file) != pixel_bytes ||
        fgetc(file) != EOF) {
        if (file) fclose(file);
        passport_storage_unlock();
        free(resource);
        return luaL_error(L, "RGB565 图片不存在或尺寸不匹配");
    }
    fclose(file);
    passport_storage_unlock();
    memset(image, 0, sizeof(*image));
    image->header.magic = LV_IMAGE_HEADER_MAGIC;
    image->header.cf = LV_COLOR_FORMAT_RGB565;
    image->header.w = (uint16_t)width;
    image->header.h = (uint16_t)height;
    image->header.stride = (uint16_t)(width * 2);
    image->data_size = (uint32_t)pixel_bytes;
    image->data = pixels;

    ++s_ui->object_count;
    lv_obj_t *object = passport_ui_object_create(
        s_ui->page, parent, PASSPORT_UI_OBJECT_IMAGE, NULL, style);
    if (!object) {
        --s_ui->object_count;
        free(resource);
        return luaL_error(L, "Image 创建失败");
    }
    lv_image_set_src(object, image);
    keep_resource(resource);
    return push_handle(L, object, style, PASSPORT_UI_OBJECT_IMAGE);
}

static int l_ui_canvas(lua_State *L)
{
    int width = check_int32(L, 1);
    int height = check_int32(L, 2);
    if (width <= 0 || width > 240 || height <= 0 || height > 320) {
        return luaL_error(L, "Canvas 尺寸超出屏幕范围");
    }
    passport_style_id_t style = check_style(L, 3, PASSPORT_STYLE_CANVAS);
    lv_obj_t *parent = optional_parent(L, 4);
    uint32_t stride = lv_draw_buf_width_to_stride(
        (uint32_t)width, LV_COLOR_FORMAT_RGB565);
    size_t bytes = (size_t)stride * (size_t)height;
    check_object_capacity(L, 1);
    passport_runtime_ui_resource_t *resource = allocate_resource(L, bytes);
    memset(resource->data, 0, bytes);
    ++s_ui->object_count;
    lv_obj_t *canvas = passport_ui_object_create(
        s_ui->page, parent, PASSPORT_UI_OBJECT_CANVAS, NULL, style);
    if (!canvas) {
        --s_ui->object_count;
        free(resource);
        return luaL_error(L, "Canvas 创建失败");
    }
    lv_canvas_set_buffer(canvas, resource->data, width, height,
                         LV_COLOR_FORMAT_RGB565);
    const passport_style_t *canvas_style = passport_theme_style(style);
    lv_canvas_fill_bg(canvas, lv_color_hex(canvas_style->background_color),
                      canvas_style->background_opacity);
    keep_resource(resource);
    return push_handle(L, canvas, style, PASSPORT_UI_OBJECT_CANVAS);
}

static int l_ui_set_text(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    if (!passport_ui_object_set_text(handle->object, handle->kind,
                                     luaL_checkstring(L, 2))) {
        return luaL_argerror(L, 1, "组件不支持文本");
    }
    return 0;
}

static int l_ui_set_style(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    passport_style_id_t style = check_style(L, 2, PASSPORT_STYLE_VIEW);
    if (!passport_ui_object_replace_style(handle->object,
                                          (passport_style_id_t)handle->style,
                                          style)) {
        return luaL_error(L, "样式替换失败");
    }
    handle->style = (uint8_t)style;
    return 0;
}

static int l_ui_set_property(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    lua_Integer property = luaL_checkinteger(L, 2);
    lua_Integer value = luaL_checkinteger(L, 3);
    if (property < 0 || property >= PASSPORT_STYLE_PROP_COUNT ||
        value < INT32_MIN || value > INT32_MAX ||
        !passport_ui_object_set_property(handle->object,
                                         (passport_style_property_t)property,
                                         (int32_t)value)) {
        return luaL_error(L, "无效样式属性值");
    }
    return 0;
}

static int l_ui_set_value(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    lua_Integer value = luaL_checkinteger(L, 2);
    if (value < INT32_MIN || value > INT32_MAX ||
        !passport_ui_object_set_value(handle->object, handle->kind,
                                      (int32_t)value, lua_toboolean(L, 3))) {
        return luaL_argerror(L, 1, "组件不支持数值");
    }
    return 0;
}

static int l_ui_set_range(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    lua_Integer minimum = luaL_checkinteger(L, 2);
    lua_Integer maximum = luaL_checkinteger(L, 3);
    if (minimum < INT32_MIN || minimum > INT32_MAX ||
        maximum < INT32_MIN || maximum > INT32_MAX ||
        !passport_ui_object_set_range(handle->object, handle->kind,
                                      (int32_t)minimum, (int32_t)maximum)) {
        return luaL_error(L, "组件不支持该数值范围");
    }
    return 0;
}

static int l_ui_set_checked(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    if (!passport_ui_object_set_checked(handle->object, handle->kind,
                                        lua_toboolean(L, 2))) {
        return luaL_argerror(L, 1, "组件不支持 checked 状态");
    }
    return 0;
}

static int l_ui_set_selected(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    if (!passport_ui_object_set_selected(handle->object, handle->kind,
                                         lua_toboolean(L, 2))) {
        return luaL_argerror(L, 1, "组件不是 ListItem");
    }
    return 0;
}

static int l_ui_set_pressed(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    if (!passport_ui_object_set_pressed(handle->object, handle->kind,
                                        lua_toboolean(L, 2))) {
        return luaL_argerror(L, 1, "组件不是 Button");
    }
    return 0;
}

static int l_ui_set_size(lua_State *L)
{
    ui_handle_t *handle = check_handle(L, 1);
    int width = check_int32(L, 2);
    int height = check_int32(L, 3);
    if (width <= 0 || width > 240 || height <= 0 || height > 320) {
        return luaL_error(L, "组件尺寸超出屏幕范围");
    }
    lv_obj_set_size(handle->object, width, height);
    return 0;
}

static int l_ui_arc_angles(lua_State *L)
{
    ui_handle_t *handle = check_kind(L, 1, PASSPORT_UI_OBJECT_ARC);
    int start = check_int32(L, 2);
    int end = check_int32(L, 3);
    if (start < 0 || start > 360 || end < 0 || end > 360) {
        return luaL_error(L, "Arc 角度必须为 0..360");
    }
    lv_arc_set_bg_angles(handle->object, start, end);
    return 0;
}

static int l_ui_spinner_params(lua_State *L)
{
    ui_handle_t *handle = check_kind(L, 1, PASSPORT_UI_OBJECT_SPINNER);
    int duration = check_int32(L, 2);
    int sweep = check_int32(L, 3);
    if (duration < 100 || duration > 10000 || sweep < 1 || sweep > 359) {
        return luaL_error(L, "Spinner 参数超出范围");
    }
    lv_spinner_set_anim_params(handle->object, (uint32_t)duration,
                               (uint32_t)sweep);
    return 0;
}

static int l_ui_image_scale(lua_State *L)
{
    ui_handle_t *handle = check_kind(L, 1, PASSPORT_UI_OBJECT_IMAGE);
    int scale = check_int32(L, 2);
    if (scale < 16 || scale > 1024) return luaL_error(L, "Image 缩放必须为 16..1024");
    lv_image_set_scale(handle->object, (uint32_t)scale);
    return 0;
}

static lv_draw_buf_t *canvas_buffer(lua_State *L, ui_handle_t **handle_out)
{
    ui_handle_t *handle = check_kind(L, 1, PASSPORT_UI_OBJECT_CANVAS);
    lv_draw_buf_t *buffer = lv_canvas_get_draw_buf(handle->object);
    if (!buffer || buffer->header.cf != LV_COLOR_FORMAT_RGB565) {
        luaL_error(L, "Canvas 缓冲无效");
    }
    if (handle_out) *handle_out = handle;
    return buffer;
}

static uint32_t check_color(lua_State *L, int argument)
{
    lua_Integer color = luaL_checkinteger(L, argument);
    if (color < 0 || color > 0xFFFFFF) luaL_argerror(L, argument, "颜色必须为 0xRRGGBB");
    return (uint32_t)color;
}

static void canvas_put(lv_draw_buf_t *buffer, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= buffer->header.w || y >= buffer->header.h) return;
    uint16_t *row = (uint16_t *)((uint8_t *)buffer->data +
                                 (size_t)y * buffer->header.stride);
    row[x] = color;
}

static int l_ui_canvas_fill(lua_State *L)
{
    ui_handle_t *handle;
    lv_draw_buf_t *buffer = canvas_buffer(L, &handle);
    uint16_t color = lv_color_to_u16(lv_color_hex(check_color(L, 2)));
    for (int y = 0; y < buffer->header.h; ++y) {
        uint16_t *row = (uint16_t *)((uint8_t *)buffer->data +
                                     (size_t)y * buffer->header.stride);
        for (int x = 0; x < buffer->header.w; ++x) row[x] = color;
    }
    lv_obj_invalidate(handle->object);
    return 0;
}

static int l_ui_canvas_pixel(lua_State *L)
{
    ui_handle_t *handle;
    lv_draw_buf_t *buffer = canvas_buffer(L, &handle);
    int x = check_int32(L, 2);
    int y = check_int32(L, 3);
    if (x < 0 || y < 0 || x >= buffer->header.w || y >= buffer->header.h) {
        return luaL_error(L, "Canvas 像素坐标越界");
    }
    canvas_put(buffer, x, y,
               lv_color_to_u16(lv_color_hex(check_color(L, 4))));
    lv_obj_invalidate(handle->object);
    return 0;
}

static int l_ui_canvas_line(lua_State *L)
{
    ui_handle_t *handle;
    lv_draw_buf_t *buffer = canvas_buffer(L, &handle);
    int x0 = check_int32(L, 2);
    int y0 = check_int32(L, 3);
    int x1 = check_int32(L, 4);
    int y1 = check_int32(L, 5);
    if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
        x0 >= buffer->header.w || x1 >= buffer->header.w ||
        y0 >= buffer->header.h || y1 >= buffer->header.h) {
        return luaL_error(L, "Canvas 线段端点越界");
    }
    uint16_t color = lv_color_to_u16(lv_color_hex(check_color(L, 6)));
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        canvas_put(buffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
    lv_obj_invalidate(handle->object);
    return 0;
}

static int l_ui_canvas_rect(lua_State *L)
{
    ui_handle_t *handle;
    lv_draw_buf_t *buffer = canvas_buffer(L, &handle);
    int x = check_int32(L, 2);
    int y = check_int32(L, 3);
    int width = check_int32(L, 4);
    int height = check_int32(L, 5);
    if (x < 0 || y < 0 || x >= buffer->header.w || y >= buffer->header.h ||
        width <= 0 || height <= 0 ||
        width > buffer->header.w - x || height > buffer->header.h - y) {
        return luaL_error(L, "Canvas 矩形越界");
    }
    uint16_t color = lv_color_to_u16(lv_color_hex(check_color(L, 6)));
    int right = x + width;
    int bottom = y + height;
    for (int row = y; row < bottom; ++row) {
        for (int column = x; column < right; ++column) {
            canvas_put(buffer, column, row, color);
        }
    }
    lv_obj_invalidate(handle->object);
    return 0;
}

static int l_ui_action(lua_State *L)
{
    if (!s_ui || !s_ui->page) return luaL_error(L, "当前没有页面");
    passport_ui_page_set_action(s_ui->page, luaL_optstring(L, 1, ""));
    return 0;
}

static int l_ui_status_bar(lua_State *L)
{
    if (!s_ui || !s_ui->page) return luaL_error(L, "当前没有页面");
    passport_ui_page_set_status_bar(s_ui->page, lua_toboolean(L, 1));
    return 0;
}

static int l_ui_key_bar(lua_State *L)
{
    if (!s_ui || !s_ui->page) return luaL_error(L, "当前没有页面");
    passport_ui_page_set_key_bar(s_ui->page, lua_toboolean(L, 1));
    return 0;
}

static void set_enum(lua_State *L, const char *name,
                     const lua_enum_value_t *values)
{
    lua_newtable(L);
    for (size_t i = 0; values[i].name; ++i) {
        lua_pushinteger(L, values[i].value);
        lua_setfield(L, -2, values[i].name);
    }
    lua_setfield(L, -2, name);
}

void passport_runtime_ui_register(lua_State *L,
                                  passport_runtime_ui_state_t *state)
{
    static const luaL_Reg functions[] = {
        {"view", l_ui_view}, {"text", l_ui_text}, {"button", l_ui_button},
        {"image", l_ui_image}, {"list", l_ui_list},
        {"list_item", l_ui_list_item}, {"bar", l_ui_bar}, {"arc", l_ui_arc},
        {"slider", l_ui_slider}, {"switch", l_ui_switch},
        {"spinner", l_ui_spinner}, {"line", l_ui_line},
        {"checkbox", l_ui_checkbox}, {"canvas", l_ui_canvas},
        {"set_text", l_ui_set_text}, {"set_style", l_ui_set_style},
        {"set_property", l_ui_set_property}, {"set_value", l_ui_set_value},
        {"set_range", l_ui_set_range}, {"set_checked", l_ui_set_checked},
        {"set_selected", l_ui_set_selected}, {"set_pressed", l_ui_set_pressed},
        {"set_size", l_ui_set_size},
        {"arc_angles", l_ui_arc_angles}, {"spinner_params", l_ui_spinner_params},
        {"image_scale", l_ui_image_scale}, {"canvas_fill", l_ui_canvas_fill},
        {"canvas_pixel", l_ui_canvas_pixel}, {"canvas_line", l_ui_canvas_line},
        {"canvas_rect", l_ui_canvas_rect}, {"action", l_ui_action},
        {"status_bar", l_ui_status_bar}, {"key_bar", l_ui_key_bar},
        {NULL, NULL},
    };
    static const lua_enum_value_t styles[] = {
        {"VIEW", PASSPORT_STYLE_VIEW}, {"PAGE", PASSPORT_STYLE_PAGE},
        {"SURFACE", PASSPORT_STYLE_SURFACE}, {"TEXT", PASSPORT_STYLE_TEXT},
        {"MUTED_TEXT", PASSPORT_STYLE_MUTED_TEXT},
        {"ACCENT_TEXT", PASSPORT_STYLE_ACCENT_TEXT}, {"CARD", PASSPORT_STYLE_CARD},
        {"BUTTON", PASSPORT_STYLE_BUTTON},
        {"BUTTON_PRESSED", PASSPORT_STYLE_BUTTON_PRESSED},
        {"IMAGE", PASSPORT_STYLE_IMAGE}, {"LIST", PASSPORT_STYLE_LIST},
        {"LIST_ITEM", PASSPORT_STYLE_LIST_ITEM},
        {"LIST_ITEM_SELECTED", PASSPORT_STYLE_LIST_ITEM_SELECTED},
        {"BAR", PASSPORT_STYLE_BAR}, {"INDICATOR", PASSPORT_STYLE_INDICATOR},
        {"ARC", PASSPORT_STYLE_ARC}, {"SLIDER", PASSPORT_STYLE_SLIDER},
        {"KNOB", PASSPORT_STYLE_KNOB}, {"SWITCH", PASSPORT_STYLE_SWITCH},
        {"SPINNER", PASSPORT_STYLE_SPINNER}, {"LINE", PASSPORT_STYLE_LINE},
        {"CHECKBOX", PASSPORT_STYLE_CHECKBOX}, {"CANVAS", PASSPORT_STYLE_CANVAS},
        {"DIVIDER", PASSPORT_STYLE_DIVIDER}, {NULL, 0},
    };
    static const lua_enum_value_t properties[] = {
        {"BACKGROUND_COLOR", PASSPORT_STYLE_PROP_BACKGROUND_COLOR},
        {"BACKGROUND_OPACITY", PASSPORT_STYLE_PROP_BACKGROUND_OPACITY},
        {"OPACITY", PASSPORT_STYLE_PROP_OPACITY},
        {"RADIUS", PASSPORT_STYLE_PROP_RADIUS},
        {"BORDER_COLOR", PASSPORT_STYLE_PROP_BORDER_COLOR},
        {"BORDER_WIDTH", PASSPORT_STYLE_PROP_BORDER_WIDTH},
        {"BORDER_OPACITY", PASSPORT_STYLE_PROP_BORDER_OPACITY},
        {"SHADOW_COLOR", PASSPORT_STYLE_PROP_SHADOW_COLOR},
        {"SHADOW_WIDTH", PASSPORT_STYLE_PROP_SHADOW_WIDTH},
        {"SHADOW_SPREAD", PASSPORT_STYLE_PROP_SHADOW_SPREAD},
        {"SHADOW_OPACITY", PASSPORT_STYLE_PROP_SHADOW_OPACITY},
        {"SHADOW_OFFSET_X", PASSPORT_STYLE_PROP_SHADOW_OFFSET_X},
        {"SHADOW_OFFSET_Y", PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y},
        {"PADDING", PASSPORT_STYLE_PROP_PADDING}, {"GAP", PASSPORT_STYLE_PROP_GAP},
        {"TEXT_COLOR", PASSPORT_STYLE_PROP_TEXT_COLOR},
        {"TEXT_OPACITY", PASSPORT_STYLE_PROP_TEXT_OPACITY},
        {"TEXT_ALIGN", PASSPORT_STYLE_PROP_TEXT_ALIGN},
        {"TEXT_LINE_SPACING", PASSPORT_STYLE_PROP_TEXT_LINE_SPACING},
        {"LINE_COLOR", PASSPORT_STYLE_PROP_LINE_COLOR},
        {"LINE_OPACITY", PASSPORT_STYLE_PROP_LINE_OPACITY},
        {"LINE_WIDTH", PASSPORT_STYLE_PROP_LINE_WIDTH},
        {"ARC_COLOR", PASSPORT_STYLE_PROP_ARC_COLOR},
        {"ARC_OPACITY", PASSPORT_STYLE_PROP_ARC_OPACITY},
        {"ARC_WIDTH", PASSPORT_STYLE_PROP_ARC_WIDTH}, {NULL, 0},
    };
    static const lua_enum_value_t alignments[] = {
        {"LEFT", PASSPORT_TEXT_ALIGN_LEFT}, {"CENTER", PASSPORT_TEXT_ALIGN_CENTER},
        {"RIGHT", PASSPORT_TEXT_ALIGN_RIGHT}, {NULL, 0},
    };

    s_ui = state;
    luaL_newmetatable(L, UI_HANDLE_METATABLE);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    set_enum(L, "Style", styles);
    set_enum(L, "Property", properties);
    set_enum(L, "TextAlign", alignments);
}

void passport_runtime_ui_set_page(passport_runtime_ui_state_t *state,
                                  passport_page_t *page,
                                  uint32_t generation)
{
    if (!state) return;
    state->page = page;
    state->generation = generation;
    state->object_count = 0U;
}

void passport_runtime_ui_release_page(passport_runtime_ui_state_t *state)
{
    if (!state) return;
    passport_runtime_ui_resource_t *resource = state->resources;
    while (resource) {
        passport_runtime_ui_resource_t *next = resource->next;
        free(resource);
        resource = next;
    }
    state->resources = NULL;
    state->page = NULL;
    state->buffer_used = 0U;
    state->object_count = 0U;
}
