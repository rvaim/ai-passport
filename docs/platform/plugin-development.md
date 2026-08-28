<p align="right"><strong>English</strong> · <a href="plugin-development.zh_CN.md">简体中文</a></p>

# Detailed Plugin Development Guide

This guide covers the current Passport Platform v1 Lua runtime. See the [package format](package-format.md), [system API](system-api.md), [Passport Link](passport-link.md), and [theme system](theme-system.md) for authoritative details.

## 1. Runtime model

A PAP app is one foreground Lua program. It cannot access LVGL, FreeRTOS, GPIO, audio, storage, or BLE directly. The system owns the page shell, navigation, active theme, shared font, physical keys, and Link transport.

The runtime is deliberately bounded: 80 KiB Lua heap, one visible page, eight navigation frames, 48 PAP-created LVGL objects, and 32 KiB of Image/Line/Canvas buffers per page. Route changes destroy the old LVGL tree instead of retaining hidden screens.

## 2. Minimal app

```text
my-plugin/
├── manifest.json
├── main.lua
└── README.md
```

```json
{
  "type": "app",
  "id": "com.example.demo",
  "name": "Demo",
  "version": "1.0.0",
  "api": 1,
  "runtime": "lua",
  "entry": "main.lua"
}
```

```lua
local value_number = 0
local value

local function refresh()
    passport.ui.set_text(value, "Value\n" .. tostring(value_number))
end

local function on_key(key, event)
    if event ~= passport.input.KeyEvent.CLICK and
       event ~= passport.input.KeyEvent.DOUBLE_CLICK then return end
    local amount = event == passport.input.KeyEvent.DOUBLE_CLICK and 2 or 1
    if key == passport.input.Key.UP then value_number = value_number + amount
    elseif key == passport.input.Key.DOWN then value_number = value_number - amount
    elseif key == passport.input.Key.OK then value_number = 0 end
    refresh()
end

passport.navigation.set_root("Demo", function()
    value = passport.ui.text("Value\n" .. tostring(value_number), passport.ui.Style.CARD)
    passport.ui.action("Reset")
    passport.app.on_key(on_key)
end)
```

The entry must call `passport.navigation.set_root` and successfully build its page before returning.

## 3. Manifest

An app manifest contains exactly `type`, `id`, `name`, `version`, `api`, `runtime`, and `entry`. Duplicate, missing, and unknown fields are rejected.

- `type`: `"app"`.
- `id`: 1–47 lowercase ASCII letters, digits, `.`, `_`, or `-`; also the install directory and Link namespace.
- `name`: non-empty and fewer than 48 UTF-8 bytes.
- `version`: non-empty and fewer than 20 bytes.
- `api`: integer `1`; there is no version negotiation.
- `runtime`: `"lua"`.
- `entry`: portable relative path under 96 bytes; no absolute path, backslash, empty segment, `.` or `..`.

Keep `id` stable across releases.

## 4. Navigation

Each route is a title plus a render callback:

```lua
local function show_details()
    passport.ui.text("Details", passport.ui.Style.CARD)
    passport.ui.action("Done")
    passport.app.on_key(function(key, event)
        -- Long-OK Back is system-owned and needs no handler.
    end)
end

passport.navigation.push("Details", show_details)
```

Use `set_root`, `push`, `replace`, `pop`, `depth`, and `can_pop`. Do not push from inside a render callback. Long-OK pops when possible and exits the PAP only at its root. The page-specific key callback is cleared on every route change, so register it inside each render callback. `on_message` remains app-wide.

## 5. UI and styles

Create theme-aware components with:

```lua
local card = passport.ui.view(passport.ui.Style.CARD)
local title = passport.ui.text("Title", passport.ui.Style.TEXT, card)
local button = passport.ui.button("Connect", passport.ui.Style.BUTTON, card)
local progress = passport.ui.bar(30, passport.ui.Style.BAR, card)
local list = passport.ui.list()
local first = passport.ui.list_item("First", list)
passport.ui.set_selected(first, true)
```

The runtime exposes View, Text, Button, Image, List/ListItem, Bar, Arc, Slider, Switch, Spinner, Line, Checkbox, and Canvas. Each has a matching public style and ultimately inherits `VIEW`; composite parts reuse `INDICATOR` and `KNOB`. Prefer shared styles over local values.

`set_text`, `set_style`, `set_property`, `set_value`, `set_range`, `set_checked`, `set_selected`, `set_pressed`, and `set_size` provide bounded updates. Arc, Spinner, Image, and Canvas also have type-specific helpers listed in the [system API](system-api.md). `action` sets the OK hint; `status_bar` and `key_bar` toggle the system bars. Handles are protected userdata and become invalid when their route is destroyed.

Image files are raw little-endian RGB565 resources with an exact `width * height * 2` byte size:

```lua
local logo = passport.ui.image("assets/logo.rgb565", 64, 64)
local graph = passport.ui.canvas(120, 64)
passport.ui.canvas_fill(graph, 0x101820)
passport.ui.canvas_line(graph, 0, 63, 119, 0, 0x38BDF8)
```

Image, Line, and Canvas share a 32 KiB page buffer budget. A page has a 48-object LVGL limit; Button and ListItem labels count toward it. PNG/JPEG decoders are not included. Use `passport.app.on_key` to drive selection, values, and checked state; the device has no touch input or automatic PAP focus manager.

The shared 14 px Chinese font is fixed. Keep text concise and test wrapping on 240×320. Do not ship a duplicate UI font.

## 6. Input

Keys and events are integer enums:

```lua
passport.app.on_key(function(key, event)
    if key == passport.input.Key.UP and
       event == passport.input.KeyEvent.DOUBLE_CLICK then
        -- Handle an UP double-click.
    end
end)
```

Available events are `PRESS`, `CLICK`, `DOUBLE_CLICK`, and `LONG_PRESS`. Long-OK is never delivered. `passport.input.supports_chords` is `false` on the current ADC-ladder board; simultaneous keys cannot be decoded. Do not invent OK+UP/OK+DOWN values.

The first completed sequence after screen-off can be consumed only to wake the display.

## 7. Lua and data APIs

Available standard libraries are base, table, string, math, and utf8. `io`, `os`, `debug`, and `package` are unavailable.

Use the system `passport.json` codec rather than bundling a parser. It exposes `decode`, `encode`, `array`, and `null`, bounded to 4096 bytes, 12 nesting levels, and 128 values. Lua integers are signed 32-bit values; fractional values use a 32-bit float. Reject application payloads that do not match your own exact schema or numeric bounds.

Use `passport.storage` for private persistent state. Its asynchronous `read`, `write`, `remove`, `list`, and `usage` operations identify the app from the running manifest; never put an app ID in a storage path. A callback receives a numeric `passport.storage.Error` first. Writes are atomic, updates retain data, and uninstall removes it. One operation is limited to 4096 bytes; each app has 16 files, 64 KiB of allocated data, and two outstanding requests. See the [system API](system-api.md) for callback signatures and path rules.

`passport.device.code()` returns the public device code. `passport.link.send(target_code, message)` returns `ok, error`; it requires the current BLE client to be connected and subscribed. `passport.app.on_message` receives only validated frames for the foreground app namespace. Link payload is at most 200 bytes.

For time-dependent displays, synchronize the generic volatile clock from an external source with `passport.clock.sync(unix_seconds_string)`, read it with `passport.clock.now()`, and refresh the current route with `passport.app.on_tick(interval_ms, callback)`. Time values are decimal strings because the Lua runtime uses 32-bit numbers. The clock survives PAP restarts while firmware remains powered, but a device reboot or power loss requires another synchronization. Tick intervals are limited to 250–60,000 ms and are cleared with their route.

## 8. Lifecycle

The entry runs once. A second `on_key`, `on_tick`, or `on_message` registration replaces the previous callback in the same scope. Key and tick callbacks belong to the current route; the message callback belongs to the app. The optional global hook runs before shutdown:

```lua
function on_stop()
    -- Cancel or report Lua-owned work.
end
```

Callbacks execute in the UI task with the LVGL lock held. Keep them short and non-blocking; do not perform long loops or bursts of Link traffic. Storage work itself runs in the shared I/O worker; only its completion callback returns to the UI task. An accepted write can finish after `on_stop`, but its callback will not run after the VM closes.

## 9. Package limits

A `.pap` is uncompressed PAP1. It can contain at most 64 payload entries; each entry and the complete package are limited to 4 MiB. Manifest JSON is limited to 4096 bytes, and portable paths to 119 bytes. Development READMEs, `dist`, and hidden files are excluded by the packer.

The ESP32-C3 has no PSRAM. Avoid large retained tables, duplicate assets, and precomputed strings.

## 10. Build and install

```bash
python3 tools/pack_pap.py my-plugin my-plugin.pap
python3 tools/passport_cli.py install my-plugin.pap
```

The CLI requires a BLE-capable host and the Python dependencies in `tools/requirements.txt`. The shared Web Bluetooth tool under `web/` provides package installation and supported app-provisioning flows on desktop browsers.

## 11. Test

Run `./tools/validate.sh --static` while developing and `./tools/validate.sh` with ESP-IDF 5.5.3 active before delivery. Host-test state machines and protocol validation independently from the device. Device checks must still cover install/update, route push/pop/replace, long-OK Back/Home, UP/DOWN single and double clicks, screen wake, theme changes, Link traffic, and repeated launch/exit cycles.

## 12. Troubleshooting

- App missing from Plugin Manager: validate exact manifest fields, ID, API, entry path, and package CRC.
- Immediate return to launcher: inspect logs for a Lua error, missing `set_root`, failed render, or exhausted 80 KiB heap.
- Stale handle error: the route was destroyed; recreate and retain handles inside its render callback.
- No long-OK callback: expected; navigation owns it.
- No chord callback: expected on the shared ADC ladder; `supports_chords` is false.
- Link send fails: ensure a client is connected, subscribed, and using the correct app service namespace.
- Clock is invalid after launch: synchronize it once after every device reboot or power loss; do not assume a battery-backed RTC.
- Storage submission returns `BUSY`: wait for one of the two outstanding callbacks and coalesce repeated state writes.
- Saved data disappears after reinstall: uninstall intentionally removes the private data directory; an in-place update with the same ID preserves it.

## 13. Release checklist

- Exact current manifest and API `1`.
- No secrets, private identifiers, generated junk, duplicate fonts, or unused assets.
- Every route registers its key callback and stays within the object/depth bounds.
- String/event APIs use enums, not handwritten magic values.
- Static and firmware validation pass; real-device results are reported separately.
