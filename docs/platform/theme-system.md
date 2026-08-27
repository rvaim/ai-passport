<p align="right"><strong>English</strong> · <a href="theme-system.zh_CN.md">简体中文</a></p>

# Theme System

Themes use the `.pap` installer with `type: theme` and execute no code. A theme supplies sparse overrides for a fixed public style graph rather than a complete flat token table.

## Style inheritance

The platform owns these relationships:

```text
VIEW
├── PAGE
│   └── LIST
├── SURFACE
├── TEXT
│   ├── MUTED_TEXT
│   ├── ACCENT_TEXT
│   └── CHECKBOX
├── CARD
│   ├── BUTTON
│   │   └── BUTTON_PRESSED
│   ├── KNOB
│   └── LIST_ITEM
│       └── LIST_ITEM_SELECTED
├── IMAGE
├── BAR
│   ├── SLIDER
│   └── SWITCH
├── INDICATOR
├── ARC
│   └── SPINNER
├── LINE
├── CANVAS
└── DIVIDER
```

The built-in theme first resolves every property as a fallback. An installed theme can then override any non-empty subset under `styles`: ancestor overrides inherit through the graph, and a more-specific installed style wins last. Explicit zero is an override, not an omission. Unknown style or property names, duplicates, empty style objects, malformed colors, and out-of-range numbers are rejected.

```json
{
  "type": "theme",
  "id": "theme.example",
  "name": "Example",
  "version": "1.0.0",
  "api": 1,
  "styles": {
    "view": {
      "background_color": "#111318",
      "text_color": "#F4F6F8"
    },
    "card": {
      "background_color": "#1C2028",
      "radius": 4,
      "border_width": 1
    }
  }
}
```

Style JSON names are `view`, `page`, `surface`, `text`, `muted_text`, `accent_text`, `card`, `button`, `button_pressed`, `image`, `list`, `list_item`, `list_item_selected`, `bar`, `indicator`, `arc`, `slider`, `knob`, `switch`, `spinner`, `line`, `checkbox`, `canvas`, and `divider`.

## Properties and bounds

Colors are exact `#RRGGBB` strings. Opacities use 0–255.

| Property | Range |
| --- | --- |
| `background_color`, `border_color`, `shadow_color`, `text_color`, `line_color`, `arc_color` | RGB |
| `background_opacity`, `opacity`, `border_opacity`, `shadow_opacity`, `text_opacity`, `line_opacity`, `arc_opacity` | 0–255 |
| `radius` | 0–32 px |
| `border_width` | 0–4 px |
| `shadow_width` | 0–12 px |
| `shadow_spread` | 0–6 px |
| `shadow_offset_x`, `shadow_offset_y` | -8–8 px |
| `padding`, `gap` | 0–24 px |
| `text_align` | `left`, `center`, `right` |
| `text_line_spacing` | -8–16 px |
| `line_width` | 0–8 px |
| `arc_width` | 0–16 px |

Native components and PAP components reference the same resolved style objects. The UI layer builds 24 shared LVGL styles per active theme instead of copying full local styles to every object. Composite widgets reuse `INDICATOR` and `KNOB` on their LVGL parts. This keeps the model deterministic and bounded on the no-PSRAM ESP32-C3.

Themes cannot add style classes, alter the inheritance graph, replace fonts, change navigation or key semantics, modify layouts, or run scripts. Keep shadows small because blend cost grows with the affected pixel area. See the [Night](../../examples/themes/night/README.md) and [Neo-Brutalism](../../examples/themes/neo-brutalism/README.md) examples.

## Theme lifecycle

Install and update themes through the normal `.pap` package path. The system Theme app lists the built-in default and valid installed manifests. Short-OK opens a theme detail page where the selected theme can be applied or uninstalled; the uninstall action requires a second confirmation press, while long-OK always returns through the system navigator.

Uninstallation runs in the shared storage worker and first moves the theme directory into `.trash` before recursively deleting it. The built-in `default` theme is protected. If the active installed theme is removed, the system applies and persists `default` before queueing the deletion, so a failed cleanup cannot leave the device pointing at a missing theme. Theme removal does not affect PAP app data or the global app-data quota.
