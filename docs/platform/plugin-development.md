<p align="right"><strong>English</strong> · <a href="plugin-development.zh_CN.md">简体中文</a></p>

# Plugin Development

A plugin contains `manifest.json` plus a Lua entry file. IDs use lowercase ASCII, digits, dot, underscore, and hyphen. Package paths are strictly relative and may not contain traversal segments.

The Lua API exposes system-owned page creation, text, action hints, button callbacks, the public device code, and connected-peer Passport Link messaging. Apps do not call LVGL, NimBLE, BSP, FreeRTOS tasks, `io`, `os`, `debug`, or `package`.

Standard interactive pages use UP/DOWN to move a selection and OK to execute it:

```lua
passport.ui.page("Counter", true, true)
local label = passport.ui.text("Count: 0")
passport.ui.actions("Reset", "Home")
```

The left action-bar slot is system-owned and always shows the UP/DOWN icons with the localized selection label. The app supplies only the short-OK and long-OK action nouns. Long-OK behavior itself remains system Home. Keep each action noun short; overlong copy is ellipsized.

Use `tools/pack_pap.py` to build a `.pap`, `tools/inspect_pap.py` to verify it, and the optional `tools/ble_install.py` plus `bleak` to transfer it.

The foreground Lua heap is capped at 80 KiB. The system-owned 14 px / 4 bpp Noto Sans SC font covers printable ASCII, all 3,755 GB2312 level-one common ideographs, firmware punctuation, and the two navigation icons. Apps do not bundle normal UI fonts; verify less-common names and copy on-device before release.
