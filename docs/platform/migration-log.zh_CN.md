<p align="right"><a href="migration-log.md">English</a> · <strong>简体中文</strong></p>

# Demo Firmware → Passport Platform v1 迁移记录

## 删除

- `main/demo.h`
- `main/demo_display.c`
- `main/demo_button.c`
- `main/demo_audio.c`
- `main/demo_battery.c`
- `main/demo_wifi.c`
- `main/demo_ble.c`
- `main/demo_low_power.c`
- `main/demo_radio.c/.h`
- `main/ui_pixel.c/.h`
- `main/ui_pixel_math.c/.h`
- `tests/test_ui_pixel_math.c`

原因：这些文件属于硬编码演示菜单和一次性视觉 Demo；继续保留会与新的 App Manager、系统 UI 和 Link Service 重复。

## 保留

`components/bsp` 全部保留，硬件引脚、显示、按键、音频、电池、I2C 仍以它为唯一板级接口。

## 新增

- `passport_core`：设备码、持久化亮度/音量/息屏/按键音设置、存储、包安装、插件注册表、主题。
- `passport_ui`：统一中文页面、状态栏、操作栏、列表。
- `passport_link`：无系统配对 BLE、目标寻址帧、BLE 包安装。
- `passport_runtime`：受限 Lua 运行时和 App API。
- 新 `main/main.c`：中文桌面、插件管理、设置、主题和系统事件任务。
- `tools/pack_pap.py` / `inspect_pap.py` / `ble_install.py`。
- `examples/counter` 和 `examples/themes/night`。
- `docs/platform/*` 全套架构/API/协议/开发/迁移/交付文档。

## 清理原则

本次没有保留 `*_v2.c`、旧菜单兼容层或重复 BLE 实现。原 Demo BLE 被整个移除，NimBLE 生命周期只由 `passport_link` 负责；App 安装只由 `passport_package` 负责；页面栏只由 `passport_ui` 负责；设备设置只由 `passport_core` 负责，不恢复已删除的 `main/device_settings.*` 兼容层。

后续每个功能完成后必须：搜索未引用旧符号 → 删除重复实现 → 检查 include/常量 → 运行 host test → 有 IDF 环境时运行 `idf.py size-components`/`size-files`。
