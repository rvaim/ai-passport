<p align="right"><strong>English</strong> · <a href="system-api.zh_CN.md">简体中文</a></p>

# Passport System API v1

## Page navigation

Every PAP owns one system navigator. The entry script must install a root route before it returns:

```lua
passport.navigation.set_root("Title", function()
    -- Build this route's current UI here.
end)
```

`set_root(title, render)`, `push(title, render)`, and `replace(title, render)` store a render callback and rebuild only the visible page. `pop() -> boolean` returns to the previous route, `depth() -> integer` reports the current depth, and `can_pop() -> boolean` reports whether a previous route exists. The stack is bounded to eight routes. Navigation cannot be changed from inside a render callback.

The system consumes long-OK. At depth greater than one it pops the current route; at the root it stops the PAP and opens the launcher. A PAP cannot replace or veto this behavior. The bottom bar displays the localized long-press Back or Home hint automatically.

## UI and public styles

The runtime exposes protected wrappers around these enabled LVGL objects:

| Constructor | Result |
| --- | --- |
| `view(style[, parent])` | Flex-column container |
| `text(text[, style[, parent]])` | Wrapping shared-font label |
| `button(text[, style[, parent]])` | Button with a centered label |
| `image(path, width, height[, style[, parent]])` | PAP resource in little-endian RGB565 |
| `list(style[, parent])`, `list_item(text, list[, style])` | Scrollable list and selectable row |
| `bar(value[, style[, parent]])` | Progress bar |
| `arc(value[, style[, parent]])` | Arc gauge |
| `slider(value[, style[, parent]])` | Slider controlled by PAP key logic |
| `switch(checked[, style[, parent]])` | Boolean switch |
| `spinner(style[, parent])` | Animated spinner |
| `line({x1,y1,x2,y2,...}[, style[, parent]])` | Polyline with 2–64 points |
| `checkbox(text, checked[, style[, parent]])` | Checkbox |
| `canvas(width, height[, style[, parent]])` | RGB565 drawing surface |

Omitting a parent places the object in the current page content area. Only a View can be a generic parent; `list_item` requires a List. Constructors return protected handles rather than `lv_obj_t *` pointers.

Style constants are integers under `passport.ui.Style`:

`VIEW`, `PAGE`, `SURFACE`, `TEXT`, `MUTED_TEXT`, `ACCENT_TEXT`, `CARD`, `BUTTON`, `BUTTON_PRESSED`, `IMAGE`, `LIST`, `LIST_ITEM`, `LIST_ITEM_SELECTED`, `BAR`, `INDICATOR`, `ARC`, `SLIDER`, `KNOB`, `SWITCH`, `SPINNER`, `LINE`, `CHECKBOX`, `CANVAS`, and `DIVIDER`.

The platform resolves these through a fixed inheritance graph. Every style ultimately inherits View; component styles then add their semantic layer. A PAP can therefore use `CARD`, `BUTTON`, or `BAR` without copying colors, radii, borders, shadows, or text settings. Changing the active theme changes components on their next render.

The system Theme app lists the built-in `default` theme and installed theme manifests. Its detail page applies or asynchronously uninstalls an installed theme after a second confirmation; `default` cannot be removed, and removing the active theme first persists `default`. Theme management is system-owned and is not exposed as a PAP Lua API.

Additional UI calls:

- `set_text(handle, text)` updates Text, Button, ListItem, or Checkbox text.
- `passport.ui.set_style(handle, style)` replaces its public style.
- `passport.ui.set_property(handle, property, value)` applies one local override.
- `set_value(handle, value[, animate])` and `set_range(handle, min, max)` operate on Bar, Arc, and Slider.
- `set_checked(handle, checked)` operates on Switch and Checkbox; `set_selected(list_item, selected)` updates a ListItem and scrolls it into view; `set_pressed(button, pressed)` drives the themed Button pressed state from key events.
- `set_size(handle, width, height)`, `arc_angles(arc, start, end)`, `spinner_params(spinner, duration_ms, sweep)`, and `image_scale(image, scale)` configure bounded widget geometry.
- `canvas_fill`, `canvas_pixel`, `canvas_line`, and `canvas_rect` draw with numeric `0xRRGGBB` colors. Coordinates must remain inside the Canvas.
- `passport.ui.action(ok_action)` sets only the system-owned OK hint.
- `passport.ui.status_bar(visible)` and `passport.ui.key_bar(visible)` toggle system bars.

Handles become invalid as soon as their route is destroyed. Local property constants also include `LINE_COLOR`, `LINE_OPACITY`, `LINE_WIDTH`, `ARC_COLOR`, `ARC_OPACITY`, and `ARC_WIDTH` in addition to the background, border, shadow, spacing, and text properties. Colors are numeric `0xRRGGBB`; alignment values are `passport.ui.TextAlign.LEFT`, `CENTER`, and `RIGHT`. Theme defaults should be preferred to local overrides.

Each page is limited to 48 underlying PAP-created LVGL objects; a Button or ListItem also consumes one object for its label. Image, Line, and Canvas buffers share a 32 KiB page budget. An Image resource must contain exactly `width * height * 2` raw RGB565 bytes. Canvas dimensions are additionally constrained by that budget. PNG/JPEG decoders and arbitrary LVGL pointers are deliberately not exposed.

## Input enums

`passport.app.on_key(callback)` receives two integers, not strings:

- `passport.input.Key.UP`, `DOWN`, `OK`
- `passport.input.KeyEvent.PRESS`, `CLICK`, `DOUBLE_CLICK`, `LONG_PRESS`

UP and DOWN double-clicks are delivered as `DOUBLE_CLICK`. Long-OK is reserved for navigation and is never delivered. The current ESP32-C3 board connects all three buttons to one ADC resistor ladder, so simultaneous buttons cannot be identified; `passport.input.supports_chords` is `false`. Key values are bit flags to leave room for a future chord-capable board, but PAPs must not infer unavailable combinations.

The first completed key sequence after screen-off can be consumed solely to wake the display.

## Data and device APIs

- `passport.app.on_message(callback)` receives `(message, source_code)` for the foreground app namespace.
- `passport.device.code() -> string` returns the public device code.
- `passport.link.send(target_code, message) -> ok, error` sends over the current subscribed BLE connection.
- `passport.json.decode`, `encode`, `array`, and `null` provide the shared bounded JSON codec.

JSON input/output is limited to 4096 bytes, 12 nesting levels, and 128 values. Strings must be valid UTF-8 without NUL/U+0000. The device runtime uses Lua's 32-bit numeric ABI: integers must fit `-2147483648..2147483647`, while fractional numbers must remain finite and nonzero when converted to a 32-bit float. Values outside those bounds are rejected instead of being silently truncated, overflowed, or underflowed.

## Persistent app storage

Every app receives a private persistent directory selected from the running manifest ID. PAP code never supplies an app ID or a physical path and cannot access another app's data. Updating the same app ID preserves its data; uninstalling the app removes its bundle and data together.

Storage is asynchronous so Flash I/O never runs in a Lua/UI callback:

```lua
local request, error = passport.storage.write("state.json", json, function(result)
    if result == passport.storage.Error.OK then
        -- The replacement is durable.
    end
end)
```

- `read(path, callback)` calls `callback(error, data_or_nil)`.
- `write(path, data, callback)` atomically replaces one file and calls `callback(error)`.
- `remove(path, callback)` removes one file or private subtree and calls `callback(error)`.
- `list([path], callback)` calls `callback(error, entries_or_nil)`. Each entry has `name`, `size`, and `is_directory`.
- `usage(callback)` calls `callback(error, used_bytes, quota_bytes, file_count)`.

Submission returns `request_id, Error.OK`, or `nil, error` when it cannot be queued. Error integers are under `passport.storage.Error`: `OK`, `NOT_FOUND`, `INVALID_PATH`, `TOO_LARGE`, `QUOTA_EXCEEDED`, `NO_SPACE`, `BUSY`, `IO_ERROR`, `CANCELED`, and `NO_MEMORY`.

Paths are portable relative ASCII, at most 95 bytes and four segments. Absolute paths, empty segments, dot-prefixed internal names, `.`/`..`, and backslashes are rejected. A write creates its parent directories. Each app may retain at most 16 files and 64 KiB of FAT-allocated data; all app data is globally capped at 1 MiB. One read or write is limited to 4096 bytes, and at most two requests may be outstanding. Accepted writes finish after an ordinary app stop, but callbacks for a closed Lua VM are discarded.

## Runtime limits and lifecycle

Only one foreground Lua app runs at a time. It has an 80 KiB Lua heap limit, one eight-frame navigator, one visible LVGL page, at most 48 PAP-created LVGL objects, at most 32 KiB of dynamic UI buffers on that page, and two outstanding storage requests. Route changes destroy the old LVGL tree and call the destination render callback; hidden page trees are never retained.

Registering `on_key` again replaces the current route's key callback. Route changes clear it, so each render callback should register the handler for that page. `on_message` is app-wide. The optional global `on_stop()` runs before the VM closes.

Callbacks execute in the system UI task while the LVGL lock is held. Keep them non-blocking.

## C system services

Public C headers under `components/passport_*/include` cover identity, settings, storage, package installation, app registry, fixed style resolution, shared UI, navigation, BLE Link, and the single-foreground Lua runtime. PAP authors should target the Lua API instead of internal C symbols.
