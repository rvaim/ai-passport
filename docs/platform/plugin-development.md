<p align="right"><strong>English</strong> · <a href="plugin-development.zh_CN.md">简体中文</a></p>

# Detailed Plugin Development Guide

This guide explains how to build, test, package, install, and maintain a
Passport Platform v1 Lua application. It describes the APIs that are actually
available in the current firmware. The platform is intentionally small: a
plugin is a single foreground app with a system-owned page, three physical
keys, and an optional Passport Link message channel.

For the binary package format, see [`.pap` Package Format v1](package-format.md).
For the wire protocol and BLE characteristics, see [Passport Link v1](passport-link.md).
For theme packages, see [Theme System](theme-system.md).

## 1. What can be developed

There are two package kinds:

- An `app` package runs one Lua entry file in the foreground. This is the
  plugin type described by this guide.
- A `theme` package contains theme tokens and executes no code. It is not a
  Lua plugin; use the [theme guide](theme-system.md) for it.

Only one Lua app runs at a time. The launcher stops the current app before it
loads another one. Apps do not run in the background, do not create their own
FreeRTOS tasks, and do not access LVGL, NimBLE, GPIO, I2C, audio, or the file
system directly.

## 2. Create the smallest app

Use this directory layout while developing:

```text
my-plugin/
├── manifest.json
├── main.lua
└── README.md                 # development notes; not shipped
```

`manifest.json`:

```json
{
  "type": "app",
  "id": "com.example.counter",
  "name": "Counter",
  "version": "1.0.0",
  "api": 1,
  "runtime": "lua",
  "entry": "main.lua"
}
```

`main.lua`:

```lua
local count = 0

passport.ui.page("Counter", true, true)
local value = passport.ui.text("Count\n0")

local function refresh()
    passport.ui.set_text(value, "Count\n" .. tostring(count))
    passport.ui.actions("Reset", "Home")
end

passport.app.on_key(function(key, event)
    if event ~= "click" and event ~= "double" then return end
    local step = event == "double" and 2 or 1

    if key == "up" then
        count = count + step
    elseif key == "down" then
        count = count - step
    elseif key == "ok" then
        count = 0
    end
    refresh()
end)

refresh()
```

The entry script must create a page before it finishes. If the script exits
without calling `passport.ui.page`, the runtime rejects the app. Top-level Lua
code runs once at launch; callback registration normally follows page creation.

## 3. Manifest reference

The device parses the manifest as UTF-8 JSON. The following fields are
supported for an app:

| Field | Required | Rules and meaning |
| --- | --- | --- |
| `type` | yes | Must be `"app"`. |
| `id` | yes | Stable package identity. Only lowercase ASCII letters, digits, `.`, `_`, and `-`; at most 47 characters. The ID is also the install directory name and Link service namespace. |
| `name` | yes | Non-empty display name, fewer than 48 UTF-8 bytes. It is shown by the launcher and plugin manager. |
| `version` | yes | Non-empty version string, fewer than 20 bytes. The firmware does not enforce SemVer; using `MAJOR.MINOR.PATCH` is recommended. |
| `api` | yes | Must be the current integer API version `1`. There is no version negotiation. |
| `runtime` | yes | Must be `"lua"`. |
| `entry` | yes | Safe package-relative path to the entry file; at most 95 bytes. `..`, `.`, empty path segments, absolute paths, backslashes, and non-portable characters are rejected. |

The seven fields above are the complete current app schema. Missing, duplicate,
or unknown fields are rejected by both the packer and firmware. The unreleased
`permissions` placeholder is not part of the schema and must not be emitted.

Keep `id` unchanged when publishing an update. Changing it creates a different
app and a different Link namespace instead of updating the existing app.

## 4. Runtime lifecycle

The runtime follows this sequence:

1. The system creates a fresh Lua state with an 80 KiB Lua heap limit.
2. It opens the supported standard libraries and registers the `passport`
   global table.
3. It loads and executes the manifest entry file once.
4. It checks that the script created a page, then dispatches key and Link
   events to the registered callbacks.
5. When the user returns Home, launches another app, or the app fails to
   start, the system calls the optional global `on_stop()` function, destroys
   the page, and closes the Lua state.

`on_stop` is a global hook, not `passport.app.on_stop`:

```lua
function on_stop()
    -- Release or reset Lua-owned state before the VM is closed.
end
```

There is no pause/resume hook, background execution, timer API, worker API, or
unregister API. Registering `on_key` or `on_message` again replaces the
previous callback. A Lua error during startup aborts the launch. An error in a
later callback is logged for that event while the registered callback remains
installed; keep the app's state valid so later events can still be handled.

Callbacks run in the system UI task while the UI lock is held. Keep them short:
update small state, update labels, and return. Avoid long loops, large string
construction, repeated Link sends, or any operation that could block.

## 5. Lua environment

The runtime opens these standard libraries:

- base
- table
- string
- math
- utf8

The platform does not expose `io`, `os`, `debug`, or `package`, and it does not
promise a module, file, network, storage, audio, sensor, camera, notification,
or cryptography API. Use only the documented Passport APIs for portable apps.
JSON is the exception to the standard-library list: it is a bounded system API
under `passport.json`, backed by the firmware codec and shared by every PAP.

The Lua heap limit applies to VM allocations such as tables, closures, compiled
code, and strings. It is separate from LVGL and system allocations, which are
also limited on the ESP32-C3. Keep the entry script and retained data small.

## 6. Complete Lua API

### 6.1 `passport.ui`

#### `passport.ui.page(title[, status_bar[, key_bar]])`

Creates and shows a system-owned 240x320 page.

- `title` must be a string.
- `status_bar` is optional and defaults to `true`.
- `key_bar` is optional and defaults to `true`.
- The function returns no value.
- Calling it again destroys the previous page and invalidates every text
  handle returned for that page.

The system owns the page geometry, theme, font, status bar, and action bar.
Plugins should place content through the documented text API instead of using
absolute coordinates.

#### `passport.ui.text(text) -> handle`

Creates a centered system label in the current page's content area and returns
an opaque handle. The handle is only valid until the page is replaced or the
app stops. Multiline text using `\n` is supported.

The call requires that a page already exists. It raises a Lua error when there
is no page or when the label cannot be allocated.

#### `passport.ui.set_text(handle, text)`

Replaces the text of a label created by `passport.ui.text`. It returns no value.
Only use the exact handle returned by the current page. Do not inspect it,
convert it, or reuse it after another call to `passport.ui.page`.

#### `passport.ui.actions(ok_action, long_ok_action)`

Sets the two app-owned action nouns shown in the bottom action bar. The system
adds its own localized prefixes and renders three fixed slots:

1. System-owned UP/DOWN navigation icons and selection hint.
2. `OK` plus `ok_action`.
3. The localized long-press prefix plus `long_ok_action`.

These are hints, not event bindings. UP/DOWN still arrive through
`passport.app.on_key`, and long-OK remains the system Home action. Short
action nouns fit best; labels that exceed a slot are ellipsized. Passing an
empty string or omitting an action hides that slot's text.

#### `passport.ui.status_bar(visible)`

Shows or hides the system status bar and recalculates the content area. Pass a
boolean. A page must already exist.

#### `passport.ui.key_bar(visible)`

Shows or hides the system bottom action bar and recalculates the content area.
Pass a boolean. A page must already exist.

There is deliberately no Lua list, button, image, font, color, layout, or raw
LVGL API in v1. Build interactive screens from labels and key events, and let
the active system theme provide colors, spacing, and typography.

### 6.2 `passport.app`

#### `passport.app.on_key(callback)`

Registers one callback with the signature:

```lua
function(key, event)
    -- handle the event
end
```

`key` and `event` values are:

| Value | Meaning |
| --- | --- |
| `up` | Physical UP key. |
| `down` | Physical DOWN key. |
| `ok` | Physical OK key. |
| `press` | Key-down event, intended for immediate feedback. |
| `click` | Completed single click. |
| `double` | Double-click event. |
| `long` | Long-press event. |

The callback receives two strings and returns no value. It must be a Lua
function. A second registration replaces the first one.

The current button policy uses a 100 ms multi-click window. UP/DOWN long
events are available to the app. An OK long event is intercepted by the system
and returns to the launcher, so the app never receives `("ok", "long")`.
The first key sequence after screen-off may also be consumed solely to wake the
display.

For state-changing actions, handle `click` and `double` explicitly and ignore
`press` unless immediate behavior is intentional:

```lua
passport.app.on_key(function(key, event)
    if event ~= "click" and event ~= "double" then return end
    local amount = event == "double" and 2 or 1
    if key == "up" then
        -- increase by amount
    elseif key == "down" then
        -- decrease by amount
    elseif key == "ok" then
        -- confirm or reset
    end
end)
```

#### `passport.app.on_message(callback)`

Registers one Passport Link callback:

```lua
passport.app.on_message(function(message, source_code)
    -- message is a Lua string; source_code is XXXXX-XXXXX-X
end)
```

The callback receives the payload as a length-aware Lua string and the sender's
canonical public device code. The system has already checked the Link version,
frame length, payload CRC, target device ID, and the active app's service
namespace. Only the foreground app receives messages. A second registration
replaces the first one.

Use a small application-level format, for example a short UTF-8 JSON object or
a delimiter-separated command. The Link payload limit is 200 bytes, measured
in bytes rather than characters; non-ASCII text can use multiple bytes per
character. Split larger application data in your own protocol or keep it out of
the v1 app channel.

### 6.3 `passport.json`

The firmware exposes one JSON codec to every PAP. A plug-in does not need to
ship, import, or maintain a Lua JSON implementation.

#### `passport.json.decode(text) -> value, error`

Decodes exactly one UTF-8 JSON value. Success returns `value, nil`; failure
returns `nil, error` without raising a Lua error. JSON values map as follows:

| JSON | Lua |
| --- | --- |
| object | Table with string keys. |
| array | Table with consecutive `1..n` integer keys and a protected system array marker. |
| string | String. |
| number | Integer when exactly representable by Lua, otherwise number. |
| boolean | Boolean. |
| null | `passport.json.null`, including when nested inside an array or object. |

The null sentinel is truthy and intentionally differs from Lua `nil`, because
assigning `nil` removes a table entry. Compare it by identity:

```lua
local request, err = passport.json.decode(message)
if err then
    -- malformed or over-limit input
    return
end
if request.optional == passport.json.null then
    -- the key exists and its JSON value is null
end
```

#### `passport.json.encode(value) -> text, error`

Encodes one supported Lua value as compact UTF-8 JSON. Success returns
`text, nil`; failure returns `nil, error`.

- A table with only consecutive integer keys `1..n` is an array.
- A table with only string keys is an object.
- An ordinary empty table is `{}`. Use `passport.json.array()` for `[]`.
- `passport.json.null` encodes as JSON null. A top-level Lua `nil` also encodes
  as null, but nested `nil` cannot be retained in a Lua table.
- Sparse arrays, mixed numeric/string keys, cycles, functions, userdata,
  threads, NaN, infinity, and integers outside the JSON safe range are rejected.

#### `passport.json.array([table]) -> table` or `nil, error`

Creates an empty array table, or marks an existing compatible table as an
array. The protected marker survives `decode` and distinguishes an empty array
from an empty object. Do not replace its metatable.

```lua
local response = {
    ok = true,
    items = passport.json.array(),
    result = passport.json.null,
}
local message, err = passport.json.encode(response)
if message then
    passport.link.send(target_code, message)
end
```

The codec is deliberately bounded for the ESP32-C3: encoded or decoded text is
at most 4096 bytes, containers may be nested at most 12 levels, and one value
tree may contain at most 128 nodes. Object keys and strings must be valid UTF-8
without NUL. Duplicate object keys are rejected instead of silently choosing
one value. JSON integers are accepted only in the interoperable IEEE-754 safe
range `-9007199254740991..9007199254740991`. These limits are codec limits;
Passport Link still has its smaller 200-byte message payload limit.

### 6.4 `passport.device`

#### `passport.device.code() -> string`

Returns this device's public code in canonical `XXXXX-XXXXX-X` form, for
example `22222-22222-2`. The code is derived from the factory identity and
cannot be changed by a plugin. It is an address and typing check, not a
password or authentication token.

### 6.5 `passport.link`

#### `passport.link.send(target_code, message) -> ok, error`

Sends one type-`message` Passport Link frame to the currently connected BLE
client.

- `target_code` must be the canonical uppercase public code, with or without
  hyphens. Prefer copying a value returned by `passport.device.code()` or a
  peer discovery flow.
- `message` is a Lua string. The maximum payload is 200 bytes.
- The sender namespace is derived automatically from the current manifest
  `id`; there is no custom service-name argument.
- v1 does not actively scan for or connect to `target_code`. A mobile or peer
  client must connect to this device and subscribe to the outgoing Link
  notification first.

Return values:

| Result | Meaning |
| --- | --- |
| `true, nil` | The notification was accepted for transmission. |
| `false, error` | The device code is invalid, no subscribed BLE peer is available, the payload is too large, or the transport ran out of memory/failed. The error is a diagnostic string, not a stable application protocol. |

This channel is not authenticated or encrypted by the plugin API. Do not send
passwords, private keys, device QR secrets, or personal data. The target check
prevents common misdelivery but does not protect against a malicious nearby BLE
client.

## 7. UI and text rules

The standard page uses the active system theme and one shared 14 px / 4 bpp
Noto Sans SC font. The font covers printable ASCII, the GB2312 level-one set
of common ideographs, firmware punctuation, and the navigation icons. It does
not guarantee every Unicode character. Check uncommon names and symbols on a
real device before release.

Practical rules:

- Keep titles and action nouns short enough for their fixed slots.
- Use `\n` for a small amount of line-oriented content; do not assume a
  scrolling text view exists.
- Keep labels and retained strings bounded so the 80 KiB Lua heap remains
  available for callbacks.
- Do not bundle normal UI fonts. Plugins cannot select arbitrary font sizes or
  replace system theme tokens.
- Treat the UI as a small page, not a desktop canvas. There is no touch input
  or plugin-defined widget tree in v1.

## 8. Package and storage limits

The supported BLE installer writes the `.pap` file to staging and validates it
before replacing the installed directory. Relevant limits are:

| Limit | Current value |
| --- | ---: |
| Lua heap per foreground app | 80 KiB |
| Manifest JSON | 4096 bytes |
| BLE package transfer | 4 MiB |
| Package payload entries | 64 |
| Payload entry size | 4 MiB maximum; keep the whole package below the BLE limit |
| Payload path | fewer than 120 ASCII bytes |
| Installed app registry | 16 apps |
| JSON codec input/output | 4096 bytes |
| JSON value tree | 12 container levels / 128 nodes |
| Link message payload | 200 bytes |

The Lua app has no file or resource API. Although the packer can include extra
files, the current runtime cannot read them from Lua; avoid shipping unused
assets. Helper Lua files also cannot be imported through a supported module
API. Keep reusable code in the entry file or combine it during your own build
step.

## 9. Build and package

From the repository root, validate the source tree and create a package:

```bash
python3 tools/pack_pap.py my-plugin dist/my-plugin.pap
python3 tools/inspect_pap.py dist/my-plugin.pap
```

The packer reads `manifest.json` as UTF-8, writes the sequential `PAP1` v1
format, and computes a CRC-32 for every payload file. It skips the source
manifest as a payload entry, hidden files/directories, any path component named
`dist`, and `README.md` / `README.zh_CN.md`. Device-side validation remains the
final authority, so an apparently successful pack command does not guarantee
that installation will succeed.

For a maintained in-repository example, see the [Counter plug-in](../../examples/counter/README.md)
and its [manifest](../../examples/counter/manifest.json).

## 10. Install on a device

### Command line

The command-line installer uses `bleak`:

```bash
python3 -m pip install bleak
python3 tools/ble_install.py 22222-22222-2 dist/my-plugin.pap
```

It finds the advertised `Passport-XXXXX-XXXXX-X` device, reads the device code
again after connecting, and refuses a mismatch before sending the package. The
device code is public and is not a BLE pairing password. v1 intentionally does
not use system BLE pairing or bonding.

### Web Bluetooth

The repository also includes a dependency-free [Web Bluetooth installer](../../web/installer.html).
Serve `web/` from HTTPS or `localhost`, choose the `.pap` file, enter the
device code, and approve the browser's device access prompt. The page filters
by the Passport service UUID and verifies the device code after connecting.

After a successful install, open Plugin Manager or return to the launcher and
launch the app. If an app was already running while its package was replaced,
return Home and launch it again so a fresh Lua state loads the new entry file.

## 11. Testing workflow

### Host-side checks

Run the repository's static gate before publishing:

```bash
./tools/validate.sh --static
```

This covers repository checks, bilingual documentation checks, host protocol
tests, the system JSON codec, the package packer, and the bundled Lua Counter
plug-in test. A plugin's
business logic can be tested on the host by providing a small Lua stub for the
documented `passport` functions, as done in
[`tests/test_counter_plugin.lua`](../../tests/test_counter_plugin.lua). Test
state transitions and boundary values without depending on LVGL.

At minimum, test:

- startup creates a page and all retained text handles are valid;
- `click`, `double`, `press`, and `long` behavior is intentional;
- counters, indexes, and message lengths remain bounded;
- invalid or unexpected Link messages do not crash the callback;
- the app does not rely on `io`, `os`, `package`, or undocumented globals.

### Firmware and device checks

Build validation is separate from hardware acceptance:

```bash
./tools/validate.sh --firmware
./tools/validate.sh
```

On a real device, verify first launch, relaunch after Home, installation and
upgrade, text coverage, action-bar ellipsis, screen-wake behavior, all key
events, Link delivery with matching and non-matching app IDs, and failure when
no Link client is subscribed. Do not report a successful firmware build as a
device test.

## 12. Troubleshooting

### The app does not appear in Plugin Manager

Inspect the package and verify `type`, `id`, `name`, `version`, `api`,
`runtime`, and `entry`. The firmware requires `runtime: "lua"`, requires the
entry file to exist in the package, and requires the installed directory name
to equal the manifest ID. Invalid directories are ignored by the registry;
the registry also exposes at most 16 apps.

### The app immediately returns to the launcher

Check the serial log for a startup error. Common causes are a Lua syntax or
runtime error, an entry path that is missing, no call to `passport.ui.page`, or
exhausting the 80 KiB Lua heap while loading the script and its retained data.
Reduce top-level tables, strings, and generated code before adding workarounds.

### A label stops updating

Make sure the handle came from the current page, the callback is registered,
and the code handles the event that is actually delivered. Calling
`passport.ui.page` again invalidates all previous handles.

### The app never receives long-OK

This is intentional. Long-OK is the system Home action and cannot be claimed by
a plugin. Use an OK click or UP/DOWN events for app actions.

### `passport.link.send` returns `false`

Check the device-code checksum and uppercase spelling, the 200-byte byte limit,
the presence of a connected BLE client, and that the client subscribed to the
outgoing notification. The receiving app must use the same manifest ID so its
service namespace matches. v1 will not discover or connect to a remote device
on behalf of the app.

### Text is missing or rendered incorrectly

The shared font is intentionally bounded. Replace uncommon characters with
supported text, shorten the copy, and inspect the exact strings on hardware.
Do not add an arbitrary font as a package workaround; the runtime has no font
loading API.

### Installation fails

Run `tools/inspect_pap.py`, check CRC output, keep the package below 4 MiB for
BLE installation, remove hidden/unsupported paths, and ensure the manifest is
below 4096 bytes. The installer uses staging and rejects invalid paths,
oversized entries, malformed manifests, bad CRCs, and missing app entry files
before replacing the existing package.

## 13. Release checklist

Before distributing a plugin:

- Keep a stable lowercase `id` and increment `version`.
- Set `api` to the supported level `1`.
- Keep all entry and payload paths portable and relative.
- Keep the Lua heap, text, Link payload, and package limits in view.
- Remove development notes, hidden files, generated output, and unused assets
  from the package source directory.
- Run the packer, inspector, static gate, and firmware/device checks that apply.
- Test a fresh install, an upgrade, Home/relaunch, all key events, and Link
  failure paths.
- Never include credentials, private keys, device QR secrets, or unsanitized
  personal data in the package or logs.
