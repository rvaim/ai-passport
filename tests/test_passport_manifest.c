#include "passport_package.h"
#include "passport_theme_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define VALID_APP \
    "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"测试\"," \
    "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\",\"entry\":\"main.lua\"}"

#define VALID_STYLES \
    "\"view\":{\"background_color\":\"#112233\",\"text_color\":\"#FFFFFF\"," \
    "\"opacity\":255,\"gap\":8}," \
    "\"card\":{\"radius\":32,\"border_width\":4,\"shadow_width\":12," \
    "\"shadow_offset_x\":-8,\"shadow_offset_y\":8}," \
    "\"text\":{\"text_align\":\"center\"}," \
    "\"line\":{\"line_color\":\"#123456\",\"line_width\":8}," \
    "\"arc\":{\"arc_color\":\"#654321\",\"arc_width\":16}"

#define THEME_WITH_STYLES(styles) \
    "{\"type\":\"theme\",\"id\":\"theme.test\",\"name\":\"测试主题\"," \
    "\"version\":\"1.0.0\",\"api\":1,\"styles\":{" styles "}}"

#define VALID_THEME THEME_WITH_STYLES(VALID_STYLES)

static void expect_manifest_invalid(const char *json, passport_package_kind_t kind)
{
    passport_manifest_t manifest;
    assert(passport_package_parse_manifest_json(
               json, strlen(json), kind, &manifest) == ESP_ERR_INVALID_ARG);
}

static void expect_theme_invalid(const char *json)
{
    passport_manifest_t manifest;
    passport_theme_definition_t theme;
    assert(passport_theme_parse_manifest_json(
               json, strlen(json), &manifest, &theme) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    passport_manifest_t manifest;
    assert(passport_package_parse_manifest_json(
               VALID_APP, strlen(VALID_APP), PASSPORT_PACKAGE_APP, &manifest) == ESP_OK);
    assert(manifest.kind == PASSPORT_PACKAGE_APP);
    assert(strcmp(manifest.id, "com.example.test") == 0);
    assert(strcmp(manifest.name, "测试") == 0);
    assert(strcmp(manifest.entry, "main.lua") == 0);
    assert(manifest.api == PASSPORT_SYSTEM_API_VERSION);

    static const char app_with_space[] = VALID_APP " \r\n\t";
    assert(passport_package_parse_manifest_json(
               app_with_space, strlen(app_with_space), PASSPORT_PACKAGE_APP,
               &manifest) == ESP_OK);
    expect_manifest_invalid(VALID_APP, PASSPORT_PACKAGE_THEME);
    expect_manifest_invalid(
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"测试\"," 
        "\"version\":\"1.0.0\",\"api\":2,\"runtime\":\"lua\",\"entry\":\"main.lua\"}",
        PASSPORT_PACKAGE_APP);
    expect_manifest_invalid(
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"测试\"," 
        "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\",\"entry\":\"main.lua\"," 
        "\"permissions\":[\"ui\"]}",
        PASSPORT_PACKAGE_APP);
    expect_manifest_invalid(
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"id\":\"com.example.other\"," 
        "\"name\":\"测试\",\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\"," 
        "\"entry\":\"main.lua\"}",
        PASSPORT_PACKAGE_APP);
    expect_manifest_invalid(
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"测试\"," 
        "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\"}",
        PASSPORT_PACKAGE_APP);
    expect_manifest_invalid(VALID_APP "x", PASSPORT_PACKAGE_APP);
    expect_manifest_invalid(
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"a\\u0000b\"," 
        "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\",\"entry\":\"main.lua\"}",
        PASSPORT_PACKAGE_APP);
    static const char invalid_utf8[] =
        "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"\xFF\"," 
        "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\",\"entry\":\"main.lua\"}";
    expect_manifest_invalid(invalid_utf8, PASSPORT_PACKAGE_APP);

    assert(passport_package_id_is_valid("theme.neo-brutalism"));
    assert(!passport_package_id_is_valid("Theme.Bad"));
    assert(passport_package_path_is_safe("assets/icon.bin"));
    assert(!passport_package_path_is_safe("a//b"));
    assert(!passport_package_path_is_safe("../main.lua"));
    assert(!passport_package_path_is_safe("main\\lua"));

    passport_theme_definition_t theme;
    assert(passport_theme_parse_manifest_json(
               VALID_THEME, strlen(VALID_THEME), &manifest, &theme) == ESP_OK);
    assert(strcmp(manifest.id, "theme.test") == 0);
    assert(theme.styles[PASSPORT_STYLE_VIEW].background_color == 0x112233U);
    assert(theme.styles[PASSPORT_STYLE_CARD].radius == 32U);
    assert(theme.styles[PASSPORT_STYLE_CARD].shadow_width == 12U);
    assert(theme.styles[PASSPORT_STYLE_CARD].shadow_offset_x == -8);
    assert(theme.styles[PASSPORT_STYLE_CARD].shadow_offset_y == 8);
    assert(theme.styles[PASSPORT_STYLE_LINE].line_color == 0x123456U);
    assert(theme.styles[PASSPORT_STYLE_LINE].line_width == 8U);
    assert(theme.styles[PASSPORT_STYLE_ARC].arc_color == 0x654321U);
    assert(theme.styles[PASSPORT_STYLE_ARC].arc_width == 16U);
    assert((theme.styles[PASSPORT_STYLE_VIEW].present &
            (UINT64_C(1) << PASSPORT_STYLE_PROP_BACKGROUND_COLOR)) != 0U);
    assert(theme.styles[PASSPORT_STYLE_SURFACE].present == 0U);
    assert(passport_package_parse_manifest_json(
               VALID_THEME, strlen(VALID_THEME), PASSPORT_PACKAGE_THEME,
               &manifest) == ESP_OK);
    assert(passport_theme_parse_manifest_json(
               VALID_THEME, strlen(VALID_THEME), &manifest, NULL) == ESP_OK);

    expect_theme_invalid(THEME_WITH_STYLES("\"card\":{}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"legacy\":{\"radius\":4}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"card\":{\"legacy\":true}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"card\":{\"radius\":33}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"line\":{\"line_width\":9}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"arc\":{\"arc_width\":17}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"view\":{\"background_color\":\"112233\"}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"view\":{\"background_color\":\"#1\"}"));
    expect_theme_invalid(THEME_WITH_STYLES("\"text\":{\"text_align\":\"justify\"}"));
    expect_theme_invalid(THEME_WITH_STYLES(
        "\"card\":{\"radius\":4,\"radius\":8}"));
    expect_theme_invalid(THEME_WITH_STYLES(
        "\"card\":{\"radius\":4},\"card\":{\"radius\":8}"));
    expect_manifest_invalid(
        "{\"type\":\"theme\",\"id\":\"theme.test\",\"name\":\"测试\","
        "\"version\":\"1.0.0\",\"api\":1,\"tokens\":{}}",
        PASSPORT_PACKAGE_THEME);

    puts("Passport manifest and theme parser host tests: PASS");
    return 0;
}
