<p align="right"><strong>English</strong> · <a href="architecture.zh_CN.md">简体中文</a></p>

# Passport Platform v1 Architecture

The rewrite keeps ESP32-C3, 8 MB flash, no PSRAM, the 240x320 display, three ADC buttons, ESP-IDF, FreeRTOS, LVGL, and the existing BSP. It adds an Android-like installable-app product model without copying Android internals.

Layers are: Chinese system apps and launcher; single-foreground app manager; native system apps plus a bounded Lua runtime; Passport UI/Link/Device/Package APIs; platform services; BSP; ESP-IDF.

`passport_core` owns one NVS-backed device-settings snapshot. Fresh devices start at 50% brightness, 30% volume, and a 30-second screen timeout, with key sound off. A single 3 KiB worker coalesces NVS writes, checks inactivity four times per second, and lazily initializes the codec only for key feedback or a volume preview. Screen timeout turns off the backlight; the first physical key sequence wakes it and is consumed so it cannot activate the hidden UI.

Key invariants: no artificial button debounce delay, long-OK is system Home, apps never access LVGL/NimBLE/BSP directly, one shared 14 px / 4 bpp Noto Sans SC font covering the GB2312 level-one common set, slow file installation runs in a worker task, and every feature ends with a cleanup pass. The bottom action bar fixes UP/DOWN as selection navigation and lets apps provide only the OK and long-OK action nouns.

To protect RAM on the no-PSRAM C3, v1 keeps BLE Peripheral/Broadcaster only. Connected clients can exchange target-addressed Passport Link frames; active Central scanning is intentionally deferred until device RAM measurements justify it.
