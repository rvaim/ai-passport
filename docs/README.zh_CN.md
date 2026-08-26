# FoloToy AI Passport · Passport Platform

[English](README.md) | 简体中文

本仓库是在原始 AI Passport ESP-IDF/BSP 基线上改造的轻量插件平台。硬件保持不变：**ESP32-C3、8 MB Flash、无 PSRAM、240 × 320 竖屏、UP/DOWN/OK 三个 ADC 按键**。底层仍使用 ESP-IDF + FreeRTOS + LVGL，平台只增加应用、UI、插件包和 BLE 传输层，不重新实现操作系统内核。

## 平台原则

- **单前台 App**：同一时间只运行一个 Lua 插件，退出时销毁页面并关闭 Lua VM。
- **系统服务常驻**：BSP、插件存储、包安装、Passport Link、主题和 UI 框架由系统统一管理。
- **设备设置持久化**：亮度、音量、息屏时间和按键音由系统统一管理并保存到 NVS，插件无需也不能直接访问 BSP。
- **不做 App Store**：内置中文“插件管理”系统 App，插件和主题通过 BLE 安装。
- **公开设备码**：每台设备有唯一、可公开分享的设备码，仅用于寻址和避免发错设备，不作为安全密码。
- **中文统一 UI**：系统 App 和标准组件使用简体中文；共享 14 px / 4 bpp Noto Sans SC 字体，覆盖全部 3755 个 GB2312 一级常用汉字，插件不能携带普通 UI 字体或任意指定字号。
- **不增加人为按键防抖延迟**：按钮回调只投递 press/click/double/long 等事件，慢操作进入系统任务。
- **功能完成后清理**：删除旧实现、重复 helper、未用 include、临时代码和冗余资源，并重新运行相关测试。

## 当前结构

```text
main/                         中文桌面、插件管理、设置、主题
components/bsp/               原硬件 BSP：显示、按键、音频、电池、I2C
components/passport_core/     设备码、设置、FAT AppFS、.pap 安装、注册表、主题
components/passport_ui/       页面容器、状态栏、内容区、动作提示栏、标准组件
components/passport_link/     BLE GATT、目标设备码校验、消息与 .pap 接收
components/passport_runtime/  单前台 Lua VM 与受限 Passport Lua API
examples/counter/             “计数器”示例插件
examples/themes/night/        “夜间主题”示例
tools/                        .pap 打包/检查、BLE 安装、验证工具
docs/platform/                架构、插件/API/协议/包格式/主题/迁移文档
```

## 硬件能力边界

| 能力 | 平台接口/实现 | 约束 |
| --- | --- | --- |
| 显示 | `bsp_display_*` + `passport_ui` | 240×320 RGB565；无触摸；无 PSRAM；持久化亮度首次默认 50% |
| 输入 | `bsp_button_*` → 系统事件队列 | UP/DOWN/OK 共用 GPIO0 ADC 电阻梯；回调不做慢操作；息屏后的第一次按键序列只负责唤醒 |
| 音频 | `bsp_audio_*` | 音量首次默认 30%；按键音默认关闭；音频 codec 仅在工作任务需要时初始化；当前 Lua API 尚未开放音频 |
| 电池 | `bsp_battery_*` | 状态栏显示电量；实际精度仍取决于电池 profile |
| BLE | `passport_link` / Lua `passport.link` | V1 为 NimBLE GATT Peripheral；无系统配对；主动 Central 扫描/连接暂缓 |
| 插件存储 | FAT wear leveling `appfs` | 约 4.94 MB；`.pap` 流式安装，不整包读入 RAM |
| 字体 | 生成的 Noto Sans SC 14 px / 4 bpp | 覆盖可打印 ASCII、3755 个 GB2312 一级汉字、固件标点和两个 Font Awesome 导航图标；需真机核对清晰度 |

所有板级常量只在 [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h) 定义。普通插件不能直接访问 LVGL、NimBLE、GPIO、I2C 或 FreeRTOS Task。

## 插件体验

`.pap` 是顺序、可流式处理的插件/主题包。系统通过 BLE 接收后先验证目标设备码、包头、Manifest、路径和 CRC，再写入 staging，成功后提交到 `/passport/apps/<id>` 或 `/passport/themes/<id>`。

插件使用系统页面容器，例如：

```lua
local value = 0
passport.ui.page("计数器", true, true)
local label = passport.ui.text("计数：0")
passport.ui.actions("归零", "主页")
```

系统负责状态栏、内容区、底部动作提示、统一字体和主题；左侧固定显示上下键图标与“(选择)”，插件只提供中、右两项动作词并处理业务状态。完整示例见 [`examples/counter`](../examples/counter)。

## 文档入口

- [平台架构](platform/architecture.zh_CN.md)
- [插件开发指南](platform/plugin-development.zh_CN.md)
- [系统 API](platform/system-api.zh_CN.md)
- [Passport Link 协议](platform/passport-link.zh_CN.md)
- [`.pap` 包格式](platform/package-format.zh_CN.md)
- [主题系统](platform/theme-system.zh_CN.md)
- [迁移与删除/新增记录](platform/migration-log.zh_CN.md)
- [构建与交付状态](platform/build-and-delivery.zh_CN.md)
- [构建与验证](development/build-and-test.zh_CN.md)
- [硬件事实与真机验收](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
- [全部文档索引](INDEX.zh_CN.md)

## 验证状态

仓库提供不依赖硬件的 Passport Link 协议、设置状态机、`.pap` 打包和字库检查。固件编译要求 ESP-IDF 5.5.3 及 ESP32-C3 工具链。任何交付都必须把 `Build`、`Host tests`、`Device tests` 分开报告；没有实际构建出的固件时不得用空文件或占位二进制冒充 BIN。
