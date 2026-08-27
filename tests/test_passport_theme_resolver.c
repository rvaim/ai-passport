#include "passport_theme_resolver.h"

#include <assert.h>
#include <stdio.h>

#define PROPERTY_BIT(property) (UINT64_C(1) << (property))

int main(void)
{
    passport_style_t resolved[PASSPORT_STYLE_COUNT];
    passport_theme_resolve(NULL, resolved);
    assert(resolved[PASSPORT_STYLE_VIEW].background_color == 0xF5F2E8U);
    assert(resolved[PASSPORT_STYLE_PAGE].background_color == 0xF5F2E8U);
    assert(resolved[PASSPORT_STYLE_TEXT].background_opacity == 0U);
    assert(resolved[PASSPORT_STYLE_MUTED_TEXT].text_color == 0x66727AU);
    assert(resolved[PASSPORT_STYLE_BUTTON].radius == 4U);
    assert(resolved[PASSPORT_STYLE_BUTTON_PRESSED].background_color == 0x1677FFU);
    assert(resolved[PASSPORT_STYLE_IMAGE].background_opacity == 0U);
    assert(resolved[PASSPORT_STYLE_LIST].padding == 0U);
    assert(resolved[PASSPORT_STYLE_LIST_ITEM].radius == 4U);
    assert(resolved[PASSPORT_STYLE_LIST_ITEM_SELECTED].text_color == 0xFFFFFFU);
    assert(resolved[PASSPORT_STYLE_SLIDER].background_color == 0xD8DCE0U);
    assert(resolved[PASSPORT_STYLE_INDICATOR].arc_color == 0x1677FFU);
    assert(resolved[PASSPORT_STYLE_LINE].line_width == 2U);
    assert(resolved[PASSPORT_STYLE_SPINNER].arc_width == 4U);

    passport_theme_definition_t installed = {0};
    installed.styles[PASSPORT_STYLE_VIEW] = (passport_style_t) {
        .present = PROPERTY_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   PROPERTY_BIT(PASSPORT_STYLE_PROP_TEXT_COLOR) |
                   PROPERTY_BIT(PASSPORT_STYLE_PROP_LINE_COLOR) |
                   PROPERTY_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   PROPERTY_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH),
        .background_color = 0x010203,
        .text_color = 0xAABBCC,
        .line_color = 0x334455,
        .border_width = 2,
        .shadow_width = 3,
    };
    installed.styles[PASSPORT_STYLE_CARD] = (passport_style_t) {
        .present = PROPERTY_BIT(PASSPORT_STYLE_PROP_RADIUS) |
                   PROPERTY_BIT(PASSPORT_STYLE_PROP_BORDER_COLOR),
        .radius = 0,
        .border_color = 0x123456,
    };
    passport_theme_resolve(&installed, resolved);
    assert(resolved[PASSPORT_STYLE_PAGE].background_color == 0x010203U);
    assert(resolved[PASSPORT_STYLE_SURFACE].background_color == 0x010203U);
    assert(resolved[PASSPORT_STYLE_TEXT].text_color == 0xAABBCCU);
    assert(resolved[PASSPORT_STYLE_MUTED_TEXT].text_color == 0xAABBCCU);
    assert(resolved[PASSPORT_STYLE_BUTTON].radius == 0U);
    assert(resolved[PASSPORT_STYLE_BUTTON].background_color == 0x010203U);
    assert(resolved[PASSPORT_STYLE_BUTTON].border_width == 2U);
    assert(resolved[PASSPORT_STYLE_BUTTON].shadow_width == 3U);
    assert(resolved[PASSPORT_STYLE_LINE].line_color == 0x334455U);
    assert(resolved[PASSPORT_STYLE_CARD].radius == 0U);
    assert(resolved[PASSPORT_STYLE_LIST_ITEM].radius == 0U);
    assert(resolved[PASSPORT_STYLE_LIST_ITEM].border_color == 0x123456U);
    assert(resolved[PASSPORT_STYLE_LIST_ITEM_SELECTED].radius == 0U);

    puts("Passport theme resolver host tests: PASS");
    return 0;
}

#undef PROPERTY_BIT
