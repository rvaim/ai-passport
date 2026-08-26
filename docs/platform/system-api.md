<p align="right"><strong>English</strong> · <a href="system-api.zh_CN.md">简体中文</a></p>

# Passport System API v1

## Lua API

### `passport.ui.page(title, status_bar, key_bar)`

Create and show a system page. The system owns the 240×320 layout, theme, font, and bars.

### `passport.ui.text(text) -> handle`

Create shared 14 px Chinese text in the current content area and return an opaque handle.

### `passport.ui.set_text(handle, text)`

Update text created by the system.

### `passport.ui.actions(ok_action, long_ok_action)`

Set the two app-owned action nouns in the bottom bar. The system renders three fixed slots:

- left: Font Awesome UP/DOWN icons plus the localized selection label;
- center: `OK ` plus `ok_action`;
- right: the localized long-press prefix plus `long_ok_action`.

Use two to four Chinese characters per action. Overflow is ellipsized. These are hints, not event bindings: UP/DOWN remain ordinary key events, and long-OK is always intercepted by the system as Home.

### `passport.ui.status_bar(visible)` / `passport.ui.key_bar(visible)`

Show or hide a system bar at runtime; the content area is recalculated automatically.

### `passport.app.on_key(callback)`

Register for key events. Long-OK is a system Home action and is not delivered to the app.

### `passport.app.on_message(callback)`

Receive Passport Link messages for the foreground app namespace.

### `passport.device.code() -> string`

Return the public device code, for example `ABCDE-FGHIJ-K`. Apps cannot change it.

### `passport.link.send(target_code, message) -> ok, error`

Send a target-addressed message over the current BLE connection. V1 does not actively scan for or connect to the target; the call fails when no client is connected or subscribed.

## C system services

C services are split across identity, persistent device settings, storage, package installation, app registry, theme, UI, BLE Link, and the single-foreground Lua runtime. `passport_settings_*` owns brightness, volume, screen timeout, key sound, inactivity tracking, and wake suppression; fresh or invalid NVS state resolves to 50%, 30%, 30 seconds, and key sound off. These controls are system-only and are not exposed to Lua. Public C headers live under each `components/passport_*/include` directory. Plugin authors should target the Lua API rather than internal C implementation symbols.
