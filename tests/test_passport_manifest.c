#include "passport_package.h"
#include "passport_theme_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define VALID_APP \
    "{\"type\":\"app\",\"id\":\"com.example.test\",\"name\":\"测试\"," \
    "\"version\":\"1.0.0\",\"api\":1,\"runtime\":\"lua\",\"entry\":\"main.lua\"}"

#define VALID_TOKENS \
    "\"background\":\"#112233\",\"surface\":\"#223344\"," \
    "\"item_background\":\"#334455\",\"text\":\"#FFFFFF\"," \
    "\"muted_text\":\"#AABBCC\",\"accent\":\"#3366CC\"," \
    "\"selection_text\":\"#FFFFFF\",\"divider\":\"#111111\"," \
    "\"border\":\"#010203\",\"shadow\":\"#000000\"," \
    "\"spacing\":8,\"radius\":32,\"border_width\":4,\"shadow_width\":12," \
    "\"shadow_spread\":6,\"shadow_opacity\":255," \
    "\"shadow_offset_x\":-8,\"shadow_offset_y\":8"

#define THEME_WITH_TOKENS(tokens) \
    "{\"type\":\"theme\",\"id\":\"theme.test\",\"name\":\"测试主题\"," \
    "\"version\":\"1.0.0\",\"api\":1,\"tokens\":{" tokens "}}"

#define VALID_THEME THEME_WITH_TOKENS(VALID_TOKENS)

static void expect_manifest_invalid(const char *json, passport_package_kind_t kind)
{
    passport_manifest_t manifest;
    assert(passport_package_parse_manifest_json(
               json, strlen(json), kind, &manifest) == ESP_ERR_INVALID_ARG);
}

static void expect_theme_invalid(const char *json)
{
    passport_manifest_t manifest;
    passport_theme_tokens_t tokens;
    assert(passport_theme_parse_manifest_json(
               json, strlen(json), &manifest, &tokens) == ESP_ERR_INVALID_ARG);
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

    passport_theme_tokens_t tokens;
    assert(passport_theme_parse_manifest_json(
               VALID_THEME, strlen(VALID_THEME), &manifest, &tokens) == ESP_OK);
    assert(strcmp(manifest.id, "theme.test") == 0);
    assert(tokens.background == 0x112233U);
    assert(tokens.radius == 32U);
    assert(tokens.shadow_width == 12U);
    assert(tokens.shadow_offset_x == -8);
    assert(tokens.shadow_offset_y == 8);
    assert(passport_package_parse_manifest_json(
               VALID_THEME, strlen(VALID_THEME), PASSPORT_PACKAGE_THEME,
               &manifest) == ESP_OK);

    expect_theme_invalid(
        THEME_WITH_TOKENS(
            "\"background\":\"#112233\",\"surface\":\"#223344\"," 
            "\"text\":\"#FFFFFF\",\"muted_text\":\"#AABBCC\"," 
            "\"accent\":\"#3366CC\",\"divider\":\"#111111\"," 
            "\"spacing\":8,\"radius\":4"));

    char invalid_color[] = VALID_THEME;
    char *color = strstr(invalid_color, "#112233");
    assert(color);
    *color = '1';
    expect_theme_invalid(invalid_color);

    char invalid_radius[] = VALID_THEME;
    char *radius = strstr(invalid_radius, "\"radius\":32");
    assert(radius);
    radius[strlen("\"radius\":3")] = '3';
    expect_theme_invalid(invalid_radius);

    expect_theme_invalid(THEME_WITH_TOKENS(VALID_TOKENS ",\"legacy\":true"));
    expect_theme_invalid(THEME_WITH_TOKENS(VALID_TOKENS ",\"background\":\"#FFFFFF\""));

    puts("Passport manifest and theme parser host tests: PASS");
    return 0;
}
