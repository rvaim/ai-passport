#include "passport_theme_resolver.h"

#include <string.h>

#define STYLE_BIT(property) (UINT64_C(1) << (property))
#define STYLE_ALL_BITS ((UINT64_C(1) << PASSPORT_STYLE_PROP_COUNT) - UINT64_C(1))

static const passport_style_id_t s_style_parent[PASSPORT_STYLE_COUNT] = {
    [PASSPORT_STYLE_VIEW] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_PAGE] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_SURFACE] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_TEXT] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_MUTED_TEXT] = PASSPORT_STYLE_TEXT,
    [PASSPORT_STYLE_ACCENT_TEXT] = PASSPORT_STYLE_TEXT,
    [PASSPORT_STYLE_CARD] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_BUTTON] = PASSPORT_STYLE_CARD,
    [PASSPORT_STYLE_BUTTON_PRESSED] = PASSPORT_STYLE_BUTTON,
    [PASSPORT_STYLE_IMAGE] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_LIST] = PASSPORT_STYLE_PAGE,
    [PASSPORT_STYLE_LIST_ITEM] = PASSPORT_STYLE_CARD,
    [PASSPORT_STYLE_LIST_ITEM_SELECTED] = PASSPORT_STYLE_LIST_ITEM,
    [PASSPORT_STYLE_BAR] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_INDICATOR] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_ARC] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_SLIDER] = PASSPORT_STYLE_BAR,
    [PASSPORT_STYLE_KNOB] = PASSPORT_STYLE_CARD,
    [PASSPORT_STYLE_SWITCH] = PASSPORT_STYLE_BAR,
    [PASSPORT_STYLE_SPINNER] = PASSPORT_STYLE_ARC,
    [PASSPORT_STYLE_LINE] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_CHECKBOX] = PASSPORT_STYLE_TEXT,
    [PASSPORT_STYLE_CANVAS] = PASSPORT_STYLE_VIEW,
    [PASSPORT_STYLE_DIVIDER] = PASSPORT_STYLE_VIEW,
};

static void apply_layer(passport_style_t *target, const passport_style_t *layer)
{
#define APPLY(property, field) \
    do { \
        if ((layer->present & STYLE_BIT(property)) != 0U) target->field = layer->field; \
    } while (0)
    APPLY(PASSPORT_STYLE_PROP_BACKGROUND_COLOR, background_color);
    APPLY(PASSPORT_STYLE_PROP_BACKGROUND_OPACITY, background_opacity);
    APPLY(PASSPORT_STYLE_PROP_OPACITY, opacity);
    APPLY(PASSPORT_STYLE_PROP_RADIUS, radius);
    APPLY(PASSPORT_STYLE_PROP_BORDER_COLOR, border_color);
    APPLY(PASSPORT_STYLE_PROP_BORDER_WIDTH, border_width);
    APPLY(PASSPORT_STYLE_PROP_BORDER_OPACITY, border_opacity);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_COLOR, shadow_color);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_WIDTH, shadow_width);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_SPREAD, shadow_spread);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_OPACITY, shadow_opacity);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_OFFSET_X, shadow_offset_x);
    APPLY(PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y, shadow_offset_y);
    APPLY(PASSPORT_STYLE_PROP_PADDING, padding);
    APPLY(PASSPORT_STYLE_PROP_GAP, gap);
    APPLY(PASSPORT_STYLE_PROP_TEXT_COLOR, text_color);
    APPLY(PASSPORT_STYLE_PROP_TEXT_OPACITY, text_opacity);
    APPLY(PASSPORT_STYLE_PROP_TEXT_ALIGN, text_align);
    APPLY(PASSPORT_STYLE_PROP_TEXT_LINE_SPACING, text_line_spacing);
    APPLY(PASSPORT_STYLE_PROP_LINE_COLOR, line_color);
    APPLY(PASSPORT_STYLE_PROP_LINE_OPACITY, line_opacity);
    APPLY(PASSPORT_STYLE_PROP_LINE_WIDTH, line_width);
    APPLY(PASSPORT_STYLE_PROP_ARC_COLOR, arc_color);
    APPLY(PASSPORT_STYLE_PROP_ARC_OPACITY, arc_opacity);
    APPLY(PASSPORT_STYLE_PROP_ARC_WIDTH, arc_width);
#undef APPLY
    target->present |= layer->present;
}

static const passport_theme_definition_t s_builtin_theme = {
    .styles = {
    [PASSPORT_STYLE_VIEW] = {
        .present = STYLE_ALL_BITS,
        .background_color = 0xF5F2E8,
        .border_color = 0xD8DCE0,
        .shadow_color = 0x000000,
        .text_color = 0x17202A,
        .line_color = 0x17202A,
        .arc_color = 0xD8DCE0,
        .background_opacity = 255,
        .opacity = 255,
        .border_opacity = 255,
        .gap = 6,
        .text_opacity = 255,
        .text_align = PASSPORT_TEXT_ALIGN_LEFT,
        .text_line_spacing = 2,
        .line_opacity = 255,
        .line_width = 2,
        .arc_opacity = 255,
        .arc_width = 4,
    },
    [PASSPORT_STYLE_PAGE] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_PADDING) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_GAP),
        .padding = 6,
        .gap = 6,
    },
    [PASSPORT_STYLE_SURFACE] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR),
        .background_color = 0xFFFFFF,
    },
    [PASSPORT_STYLE_TEXT] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_OPACITY) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_ALIGN) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_LINE_SPACING),
        .text_align = PASSPORT_TEXT_ALIGN_LEFT,
        .text_line_spacing = 2,
    },
    [PASSPORT_STYLE_MUTED_TEXT] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_COLOR),
        .text_color = 0x66727A,
    },
    [PASSPORT_STYLE_ACCENT_TEXT] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_COLOR),
        .text_color = 0x1677FF,
    },
    [PASSPORT_STYLE_CARD] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_RADIUS) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_SPREAD) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_OPACITY) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_OFFSET_X) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_OFFSET_Y) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_GAP),
        .background_color = 0xF5F2E8,
        .radius = 4,
        .padding = 6,
        .gap = 6,
    },
    [PASSPORT_STYLE_BUTTON] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_ALIGN),
        .background_color = 0xFFFFFF,
        .text_align = PASSPORT_TEXT_ALIGN_CENTER,
    },
    [PASSPORT_STYLE_BUTTON_PRESSED] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_COLOR),
        .background_color = 0x1677FF,
        .text_color = 0xFFFFFF,
    },
    [PASSPORT_STYLE_IMAGE] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_OPACITY) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING),
    },
    [PASSPORT_STYLE_LIST] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_PADDING) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_GAP),
        .gap = 4,
    },
    [PASSPORT_STYLE_LIST_ITEM_SELECTED] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_TEXT_COLOR),
        .background_color = 0x1677FF,
        .text_color = 0xFFFFFF,
    },
    [PASSPORT_STYLE_BAR] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_RADIUS) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING),
        .background_color = 0xD8DCE0,
        .radius = 4,
    },
    [PASSPORT_STYLE_INDICATOR] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_RADIUS) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_ARC_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_LINE_COLOR),
        .background_color = 0x1677FF,
        .radius = 4,
        .arc_color = 0x1677FF,
        .line_color = 0x1677FF,
    },
    [PASSPORT_STYLE_ARC] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_OPACITY) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_ARC_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_ARC_WIDTH),
        .arc_color = 0xD8DCE0,
        .arc_width = 4,
    },
    [PASSPORT_STYLE_KNOB] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_RADIUS) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING),
        .background_color = 0xFFFFFF,
        .radius = 16,
    },
    [PASSPORT_STYLE_LINE] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BACKGROUND_OPACITY) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING),
    },
    [PASSPORT_STYLE_CANVAS] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_SHADOW_WIDTH) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_PADDING),
    },
    [PASSPORT_STYLE_DIVIDER] = {
        .present = STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_COLOR) |
                   STYLE_BIT(PASSPORT_STYLE_PROP_BORDER_WIDTH),
        .border_color = 0xD8DCE0,
        .border_width = 1,
    },
    },
};

void passport_theme_resolve(const passport_theme_definition_t *installed,
                            passport_style_t out[PASSPORT_STYLE_COUNT])
{
    /* First resolve a complete built-in fallback graph. Installed theme
     * values are applied afterwards so an ancestor override is not hidden by
     * a more-specific built-in default. */
    for (size_t i = 0; i < PASSPORT_STYLE_COUNT; ++i) {
        passport_style_id_t id = (passport_style_id_t)i;
        if (id == PASSPORT_STYLE_VIEW) memset(&out[i], 0, sizeof(out[i]));
        else out[i] = out[s_style_parent[i]];
        apply_layer(&out[i], &s_builtin_theme.styles[i]);
        out[i].present = STYLE_ALL_BITS;
    }

    if (!installed) return;

    passport_style_id_t lineage[PASSPORT_STYLE_COUNT];
    for (size_t i = 0; i < PASSPORT_STYLE_COUNT; ++i) {
        size_t depth = 0U;
        passport_style_id_t id = (passport_style_id_t)i;
        for (;;) {
            lineage[depth++] = id;
            if (id == PASSPORT_STYLE_VIEW) break;
            id = s_style_parent[id];
        }
        while (depth > 0U) {
            apply_layer(&out[i], &installed->styles[lineage[--depth]]);
        }
        out[i].present = STYLE_ALL_BITS;
    }
}

#undef STYLE_BIT
#undef STYLE_ALL_BITS
