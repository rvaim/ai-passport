<p align="right"><strong>English</strong> · <a href="theme-system.zh_CN.md">简体中文</a></p>

# Theme System

Themes use the same `.pap` transport and installer with `type: theme`, but execute no code. System pages read the active tokens when created, so the launcher, native system apps, and standard plug-in UI share one visual language.

## Tokens

Colors use the exact `#RRGGBB` form. Numeric values are bounded during packaging and again when the firmware installs or loads the theme.

| Token | Range | Use |
| --- | --- | --- |
| `background` | RGB | Page and list canvas |
| `surface` | RGB | Status and action bars |
| `item_background` | RGB | Unselected standard list items |
| `text` | RGB | Primary text |
| `muted_text` | RGB | Secondary and status text |
| `accent` | RGB | Selected list item and action emphasis |
| `selection_text` | RGB | Text on selected list items |
| `divider` | RGB | Status/action bar separators |
| `border` | RGB | Standard list-item border |
| `shadow` | RGB | Standard list-item shadow |
| `spacing` | 2–12 px | Page padding and list rhythm |
| `radius` | 0–32 px | Standard list-item corner radius |
| `border_width` | 0–4 px | Standard list-item border thickness |
| `shadow_width` | 0–12 px | LVGL shadow blur width; `0` disables the shadow |
| `shadow_spread` | 0–6 px | Shadow expansion |
| `shadow_opacity` | 0–255 | Shadow opacity; `0` disables the shadow |
| `shadow_offset_x` | -8–8 px | Horizontal shadow offset |
| `shadow_offset_y` | -8–8 px | Vertical shadow offset |

All 18 tokens are required. Missing, duplicate, or unknown token names, colors without `#`, and out-of-range values are rejected before an installed directory is committed. Installed themes are never completed from built-in defaults; the built-in default theme is a separate system theme.

Borders and shadows are applied only to shared list items. Their dimensions are bounded and the list reserves the necessary draw margin, avoiding clipping without allocating image assets, extra LVGL objects, timers, or tasks. Large soft shadows still cost more pixels to blend than a zero-effect theme; use the smallest width that expresses the style.

Themes cannot replace the shared 14 px Chinese font, change hardware key semantics, alter layouts, or inject scripts. Examples include the restrained [Night theme](../../examples/themes/night/manifest.json) and the high-contrast [Neo-Brutalism theme](../../examples/themes/neo-brutalism/README.md).
