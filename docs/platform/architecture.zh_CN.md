<p align="right"><a href="architecture.md">English</a> · <strong>简体中文</strong></p>

# Passport Platform v1 架构

## 目标与硬件约束

本次改造保留 ESP32-C3、8 MB Flash、无 PSRAM、240×320 屏幕、三个 ADC 按键以及现有 BSP。底层仍是 ESP-IDF + FreeRTOS + LVGL，不重新实现内核。产品层提供“像 Android 一样可安装 App”的体验，但内部坚持单前台 App、常驻系统服务和显式资源上限。

## 分层

```text
桌面 / 设置 / 插件管理 / 主题
              ↓
        单前台 App 管理
      ↙                 ↘
原生系统 App          Lua 插件
              ↓
        Passport System API
   UI / Link / Device / Package
              ↓
     passport_core / ui / link
              ↓
              BSP
              ↓
      ESP-IDF / FreeRTOS / LVGL
```

### `components/bsp`

只保存板级硬件能力，不让插件直接访问 GPIO、I2C、I2S、ADC 或 LVGL。

### `passport_core`

设备公开身份码、设备设置、wear-levelled FAT 存储、`.pap` 安装事务、插件注册表和主题令牌。设备设置以一份一致快照保存在 NVS；首次启动默认亮度 50%、音量 30%、息屏时间 30 秒，按键音默认关闭。

单一 3 KiB 工作任务负责合并 NVS 写入、每 250 ms 检查一次无操作超时，并且仅在播放按键反馈或音量试听时按需初始化音频 codec。息屏只关闭背光；第一次物理按键序列会被消费并仅用于唤醒，避免误触发隐藏界面。

### `passport_ui`

统一 240×320 页面容器。页面可选择显示状态栏和底部操作栏；左侧的上下键图标与“(选择)”由系统固定，中间和右侧分别显示插件提供的短按确定、长按确定动作词。插件不自己绘制系统栏。统一使用覆盖 GB2312 一级常用汉字的 14 px / 4 bpp Noto Sans SC 字体。

### `passport_link`

不做 BLE 系统配对。设备码公开，用于寻址、防误发和目标复核，而不是安全认证。V1 固件作为 GATT Peripheral 接收移动端/其他 GATT Client 的连接；数据帧仍携带 source/target ID 并在系统层校验。

### `passport_runtime`

单 Lua VM、单前台 App。Lua heap 设 80 KiB 上限；只开放 base/table/string/math/utf8，关闭 `io`、`os`、`debug`、`package`。退出 App 后销毁页面并关闭 Lua state，释放整个运行时。

## 不可破坏的约束

- 不增加人为按键 debounce delay；保留按下/单击/双击/长按状态事件。
- 长按确定由系统保留为“返回桌面”。
- 普通插件不直接调用 LVGL、NimBLE、BSP 或 FreeRTOS task API。
- UI 文案和系统 App 使用中文。
- 插件不能携带普通 UI 字体，也不能自由指定字号。
- 慢速安装/文件写入在工作任务执行，不在按键或 NimBLE GATT 回调里直接做重活。
- 每完成一个功能必须做 cleanup pass：删除旧实现、死代码、重复 helper、临时日志和无用资源，再执行测试/空间检查。

## V1 明确限制

为控制 ESP32-C3 RAM，当前 BLE 固件仅开启 Peripheral/Broadcaster，不开启 Central/Observer。`passport.link` 已提供目标 ID/服务命名空间/消息帧，支持当前已连接客户端的数据交换；“设备主动扫描指定 Passport 并作为 Central 建连”留给后续版本。此限制必须在真机 RAM 基线出来后再决定是否打开，不能在无 PSRAM 平台上默认常驻两套角色。
