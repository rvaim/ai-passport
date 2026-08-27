<p align="right"><strong>English</strong> · <a href="architecture.zh_CN.md">简体中文</a></p>

# Passport Platform v1 Architecture

The rewrite keeps ESP32-C3, 8 MB flash, no PSRAM, the 240x320 display, three ADC buttons, ESP-IDF, FreeRTOS, LVGL, and the existing BSP. It adds an Android-like installable-app product model without copying Android internals.

Layers are: Chinese system apps and launcher; single-foreground app manager; native system apps plus a bounded Lua runtime; Passport UI/Link/Device/Package APIs; platform services; BSP; ESP-IDF.

`passport_core` owns one NVS-backed device-settings snapshot. Fresh devices start at 50% brightness, 30% volume, and a 30-second screen timeout, with key sound off. A single 3 KiB worker coalesces NVS writes, checks inactivity four times per second, and lazily initializes the codec only for key feedback or a volume preview. Screen timeout turns off the backlight; the first physical key sequence wakes it and is consumed so it cannot activate the hidden UI.

Installed apps are containers under `apps/<id>` with an immutable `bundle` and a private `data` subtree on the existing wear-levelled FAT partition. Updates swap only the bundle; uninstall atomically hides the whole container before background deletion. PAPs access data only through the asynchronous `passport.storage` API, never by app ID or physical path. One shared worker bounds Flash I/O to two outstanding requests per runtime, 4096 bytes per file operation, 16 files, 64 KiB per app, and 1 MiB globally. A blank partition may be initialized once; a damaged nonblank filesystem is reported instead of silently formatted.

Key invariants: no artificial button debounce delay, a 100 ms multi-click window, native UP/DOWN double-click as a two-row move, and 800 ms long-OK owned by one navigator as Back at secondary depth and Home at a root. Apps never access raw LVGL/NimBLE/BSP APIs. Native pages and PAPs share 24 inherited public styles and one 14 px / 4 bpp Noto Sans SC font. PAP wrappers cover the enabled LVGL View, Text, Button, Image, List, Bar, Arc, Slider, Switch, Spinner, Line, Checkbox, and Canvas objects. Only one visible LVGL page tree is retained; navigation stores at most eight lightweight frames, PAP pages are capped at 48 LVGL objects, and Image/Line/Canvas share 32 KiB of dynamic buffers. Input callbacks receive integer key/event enums. The current ADC ladder cannot identify simultaneous keys, so chord support is reported as unavailable. Slow file installation stays in a worker task, and cleanup remains part of every feature. The bounded runtime also exposes the firmware's cJSON codec as `passport.json`, avoiding bundled parsers.

To protect RAM on the no-PSRAM C3, v1 keeps BLE Peripheral/Broadcaster only. Connected clients can exchange target-addressed Passport Link frames; active Central scanning is intentionally deferred until device RAM measurements justify it.
