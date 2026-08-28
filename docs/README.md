# FoloToy AI Passport · Passport Platform

English | [简体中文](README.zh_CN.md)

This repository turns the original AI Passport ESP-IDF/BSP baseline into a small installable-app platform while keeping the hardware unchanged: **ESP32-C3, 8 MB Flash, no PSRAM, a 240×320 portrait display, and UP/DOWN/OK on an ADC resistor ladder**. ESP-IDF, FreeRTOS, LVGL, and the existing BSP remain the foundation; this project adds an application, UI, package, and BLE transport layer rather than implementing a new kernel.

## Platform rules

- **One foreground app**: only one Lua plug-in VM runs at a time and is destroyed on exit.
- **System services stay resident**: BSP, package storage, Passport Link, themes, UI, and app registry are system-owned.
- **Persistent device settings**: brightness, volume, screen timeout, and key sound are system-owned, stored in NVS, and applied without giving plug-ins direct BSP access.
- **No App Store**: a Chinese plug-in manager system app receives apps/themes over BLE.
- **Public device code**: each device exposes a stable human-readable code, presented as the pairing code in the installer, for broadcast targeting and misdelivery prevention rather than security.
- **Unified Chinese UI**: system apps and standard components use Simplified Chinese and one shared 14 px / 4 bpp Noto Sans SC font covering all 3,755 GB2312 level-one common ideographs.
- **No artificial button debounce delay**: callbacks only dispatch lightweight events; slow work runs in system/worker tasks.
- **Cleanup is part of Done**: obsolete implementations, duplicate helpers, unused includes, temporary code, and redundant assets are removed before delivery.

## Repository layout

```text
main/                         Chinese launcher, plug-in manager, settings, themes
components/bsp/               Existing board BSP
components/passport_core/     identity, settings, FAT AppFS, .pap installer, registry, themes
components/passport_ui/       page container, status bar, content, action-hint bar, widgets
components/passport_link/     BLE GATT, target-code checks, messages, .pap receive path
components/passport_runtime/  one bounded Lua VM and the Passport Lua API
examples/agent-auth-panel/    Agent authorization panel plug-in
examples/totp-authenticator/  persistent TOTP 2FA authenticator plug-in
examples/themes/night/        lightweight night-theme example
tools/                        package, inspection, BLE install, and validation tools
web/                          shared Web Bluetooth installer and 2FA provisioning tool
site/                         GitHub Pages builder for the shared device tool
docs/platform/                architecture/API/protocol/package/theme/migration docs
```

## Hardware capability boundary

| Capability | Platform interface | Boundary |
| --- | --- | --- |
| Display | `bsp_display_*` + `passport_ui` | 240×320 RGB565, no touch, no PSRAM; persisted brightness defaults to 50% |
| Input | `bsp_button_*` → system queue | UP/DOWN/OK share GPIO0 ADC ladder; 100 ms multi-click resolution, native double-navigation by two rows, and 800 ms long-OK; the first key sequence after screen-off only wakes the display |
| Audio | `bsp_audio_*` | volume defaults to 30%; key sound defaults off; the codec is initialized lazily in a worker and is not exposed to Lua in v1 |
| Battery | `bsp_battery_*` | shown by the status bar; calibration still depends on the cell/profile |
| BLE | `passport_link` / Lua `passport.link` | NimBLE GATT Peripheral in v1; no OS pairing; active Central scanning is deferred |
| Plug-in storage | wear-levelled FAT `appfs` | about 5.94 MiB; packages are streamed instead of buffered whole in RAM |
| Font | generated Noto Sans SC 14 px / 4 bpp | printable ASCII, 3,755 GB2312 level-one ideographs, firmware punctuation, and two Font Awesome navigation icons; verify legibility on-device |

Board constants remain exclusively in [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h). Normal plug-ins do not access LVGL, NimBLE, GPIO, I2C, or FreeRTOS tasks directly.

## Plug-ins

`.pap` is a sequential streaming package for apps and themes. BLE installation verifies the target device code, package header, manifest, paths, and CRC, writes into staging, then commits to `/passport/apps/<id>` or `/passport/themes/<id>`.

The dependency-free [Passport web tool](../web/installer.html) lets desktop Chrome or Edge enter the device pairing code and connect once. The same verified GATT connection can install a local plug-in or theme `.pap` with live progress, or send an `otpauth://totp` record and browser time to the [2FA Authenticator PAP](platform/totp-authenticator.md). The browser requires one explicit device-authorization confirmation on first use; a previously authorized matching Passport reconnects without the device picker. Serve `web/` from HTTPS or localhost; local instructions and interaction states are documented in [its design note](../web/DESIGN.md). The public [GitHub Pages site](https://rvaim.github.io/ai-passport/) deploys this shared tool as its root page and does not publish a package catalog or bundled PAP files.

The system owns the status bar, content rectangle, action-hint bar, navigation, font, and inherited public styles. PAPs provide only the short-OK action noun; long-OK pops the current route and exits to the launcher only from the root. Key callbacks use integer enums, and `passport.input.supports_chords` is false because the current buttons share one ADC ladder. See the [plug-in development guide](platform/plugin-development.md) for a complete API example.

## Documentation

- [Architecture](platform/architecture.md)
- [Plug-in development](platform/plugin-development.md)
- [System API](platform/system-api.md)
- [Passport Link](platform/passport-link.md)
- [2FA Authenticator PAP](platform/totp-authenticator.md)
- [`.pap` format](platform/package-format.md)
- [Themes](platform/theme-system.md)
- [Migration/add/remove log](platform/migration-log.md)
- [Build/delivery status](platform/build-and-delivery.md)
- [Build and validation](development/build-and-test.md)
- [Hardware facts and device validation](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)
- [Documentation index](INDEX.md)

## Validation status rule

Host-side Passport Link, settings-state-machine, `.pap`, and font checks are available without hardware. Firmware compilation requires ESP-IDF 5.5.3 and the ESP32-C3 toolchain. Every delivery must report `Build`, `Host tests`, and `Device tests` separately. An empty or placeholder binary must never be presented as a built firmware image.
