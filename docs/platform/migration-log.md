<p align="right"><strong>English</strong> · <a href="migration-log.zh_CN.md">简体中文</a></p>

# Migration Log

The hard-coded demo registry, demo pages, old pixel UI helper, and its host test were removed. The BSP is retained unchanged as the hardware boundary.

New components are `passport_core`, `passport_ui`, `passport_link`, and `passport_runtime`, plus a new system launcher, package tools, counter sample, theme sample, and platform documentation. `passport_core` also owns the persistent brightness, volume, screen-timeout, and key-sound service.

The rewrite deliberately avoids parallel v2 files or compatibility wrappers: BLE ownership lives in Passport Link, package installation lives in Package Service, system bars live in Passport UI, and settings have one owner instead of restoring the removed `main/device_settings.*` compatibility layer.
