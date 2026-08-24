# AI Passport 插件固件

[English](README.md) | 简体中文

这是一个面向 **FoloToy AI Passport 硬件**的社区衍生固件，重点不是继续扩充固定 Demo，
而是把设备改造成一个可以安装受约束插件的离线小型平台。

本项目基于 FoloToy 的开源项目
[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport) 修改，复用了其 ESP32-C3
硬件支持与 BSP 基础，但本仓库不是上游镜像，也不是 FoloToy 官方固件。插件 VM、统一
Registry、主题系统、设备码、Web Bluetooth 安装、Nearby Runtime Gateway 以及本仓库的
产品界面均属于这个衍生项目的后续设计。详细来源与差异见
[`docs/PROJECT_ORIGIN.md`](docs/PROJECT_ORIGIN.md)。

## 项目目标

传统 MCU 固件通常把每项功能直接编译进应用。这个项目改用两层结构：底层固件只负责硬件、
资源隔离和稳定的系统 API；上层功能尽量通过可安装的 `.fpp` 包交付。

```text
AI Passport 硬件
  └─ ESP-IDF + BSP
      └─ 系统服务：显示 / 按键 / 音频 / 存储 / 身份 / BLE
          └─ Registry + Host API v5 + 有界字节码 VM
              ├─ 系统插件：设置、插件管理
              ├─ 下载插件：工具、游戏、通信应用
              └─ 主题插件：替换共享 UI Token
```

插件只在前台运行。插件退出、长按返回、启动失败或 VM 异常时，宿主会统一停止定时器、音频、
BLE 与传输任务，并回收缓冲区和文件句柄。插件主动 `release` 是良好习惯，但不是系统正确性
的前提。

## 当前能力

当前固件版本为 **V2.6.0**，插件格式固定为 Manifest v5 / Host API v5。

| 能力 | 当前实现 |
| --- | --- |
| 应用目录 | 系统能力与下载应用共用一个 Registry；主页不维护第二份列表 |
| 插件包 | JSON 源码编译为有界字节码，经 ECDSA P-256 签名后生成 `.fpp` |
| UI | 可选语义组件、公共状态栏/动作栏/弹窗/主题，也允许游戏自行绘制 Canvas |
| 中文字体 | 下载插件统一使用公共 14 px 字体；打包时检查字符覆盖，避免安装后出现白块 |
| 设置 | 亮度、音量、按键音、自动息屏、主题和设备信息 |
| 安装 | Chrome Web Bluetooth 连接无配对 BLE GATT；设备端仍需按 OK 批准 |
| Nearby | 设备码同步后的消息、Blob 文件和 16 kHz 半双工语音 |
| 生命周期 | 单前台 VM、代次隔离、退出强制回收、迟到事件丢弃 |
| 数据 | 插件私有整数 KV、4 个 4096-byte RAM buffer、单个 768 KiB 临时 Blob |

Nearby 的设备码用于确认网页连接的是目标设备，不是密码。每次 BLE 重连都必须重新同步。
下载插件不能直接访问 NimBLE、Wi-Fi、Socket、LVGL、FreeRTOS、NVS 或任意内存指针；它们
只能调用 Manifest 权限允许的 Host API。本版本没有 Wi-Fi、SoftAP 或 HTTP 安装后端。

## 目标硬件

固件面向上游项目所支持的 ESP32-C3 AI Passport 板卡。以下是本仓库代码和已连接设备验证过
的配置，不代表 ESP32-C3 芯片本身的全部能力：

| 子系统 | 配置 |
| --- | --- |
| MCU / Flash | ESP32-C3，8 MB Flash，无 PSRAM |
| 屏幕 | ST7789P3，240 × 320，SPI RGB565 |
| 输入 | UP / DOWN / OK 三键，共用 GPIO0 ADC 电阻梯 |
| 音频 | ES8311，I2S 播放与麦克风采集 |
| 电量 | CW2017，通过共享 I2C 读取 SOC 与电压 |
| 调试 | ESP32-C3 原生 USB Serial/JTAG |

引脚、ADC 电压窗、面板初始化和线程约束以
[`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h) 与
[`docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md`](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md)
为准。仓库没有原理图、PCB 或 BOM，因此不能凭空承诺触摸、IMU、外部存储、充电控制或
任意“空闲 GPIO”。

## 快速开始

准备以下环境：

- ESP-IDF 5.5.x；当前已验证版本为 5.5.3；
- Python 3；插件工具需要 `cryptography`，BLE 命令行工具需要 `bleak`；
- 桌面版或 Android Chrome，用于 Web Bluetooth 安装。

```bash
git clone https://github.com/rvaim/ai-passport.git
cd ai-passport

python3 -m pip install -r tools/requirements.txt
tests/run_host_tests.sh

source /path/to/esp-idf-v5.5.3/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

`tests/run_host_tests.sh` 会检查字体覆盖、包格式、VM、设备码、插件管理模型、Nearby 帧与
ADPCM、参考插件和 Python 工具。`idf.py build` 只证明目标可以编译；屏幕、三键、音频、
电量和 BLE 行为仍需在真机上验收。

## 安装插件

1. 在设备主页进入“插件”，记下设备码并保持该页面打开。
2. 在仓库根目录启动静态服务：

   ```bash
   python3 -m http.server 8000 --directory web
   ```

3. 用桌面或 Android Chrome 打开 `http://localhost:8000/installer.html`。
4. 输入设备码并选择 `Passport-XXXX`。Chrome 首次授权必须显示设备选择器，这是浏览器规则。
5. 选择 `.fpp` 文件并发送；设备验签后短按 OK 批准。

网页只有在设备停留于“插件”页时才能发现 Installer Service。普通 app 打开并申请 Nearby
后，固件会切换到另一套 Runtime Service；两种 BLE profile 不会同时运行。

参考包源码位于 [`examples/plugins/`](examples/plugins/)：

- `counter`：状态、按键与 KV；
- `settings`：系统设置和语义 UI；
- `meteor-tap`：自绘游戏界面；
- `midnight-theme`：纯数据主题；
- `nearby-demo`：消息、Blob 和前台通信租约。

仓库默认忽略生成的 `.fpp` 和私钥。若要自己打包，请先阅读完整规范，不要把
`.keys/plugin-signing-private.pem` 提交到 Git。

## 开发插件

插件唯一人工维护的源码是 `plugin.json`。下面的命令会校验 schema、权限、字体、跳转目标、
状态槽和 Host API 版本，然后签名：

```bash
python3 tools/plugin_tool.py pack \
  examples/plugins/counter/plugin.json \
  --private .keys/plugin-signing-private.pem \
  --output /tmp/counter.fpp

python3 tools/plugin_tool.py inspect /tmp/counter.fpp
```

插件不是本地动态库，也不能装入原生 C 代码。需要新的通用能力时，应先把能力设计成有界的
宿主服务，再通过权限和 Host API 暴露，而不是让插件绕过系统直接访问硬件。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [`PLUGIN_DEVELOPMENT_GUIDE.md`](docs/PLUGIN_DEVELOPMENT_GUIDE.md) | JSON 规范、指令集、UI、权限、签名、测试与发布清单 |
| [`PLUGIN_SYSTEM.md`](docs/PLUGIN_SYSTEM.md) | Registry、VM、BLE 协议、Flash 布局和生命周期实现 |
| [`AI_HARDWARE_DEVELOPMENT_GUIDE.md`](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md) | 当前板卡事实、BSP、内存、线程规则和真机验收 |
| [`PROJECT_ORIGIN.md`](docs/PROJECT_ORIGIN.md) | 上游来源、继承范围、主要差异与同步原则 |
| [`AGENTS.md`](AGENTS.md) | 在本仓库工作的代码、验证和提交约束 |

## 仓库结构

```text
components/bsp/             上游基础上继续维护的板级驱动
components/plugin_runtime/  包格式、签名、Flash 槽、安装器与字节码 VM
main/                       Registry、系统插件、宿主服务、Nearby 与产品 UI
examples/plugins/           参考插件 JSON 源码
tools/                      打包、字体、BLE 安装和 Nearby 客户端工具
tests/                      可在电脑运行的协议、模型、VM、字体和插件测试
web/                        本地 Web Bluetooth 安装页
docs/                       架构、开发规范、硬件事实和项目来源
```

## 上游与许可证

上游项目：[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport)。原项目由 FoloToy
以 MIT License 发布；本仓库保留原始 [`LICENSE`](LICENSE) 和版权声明。修改版同样按该
许可证条款分发。

“FoloToy”与“AI Passport”用于说明兼容硬件和代码来源，不表示上游为本衍生项目提供背书。
提交问题时请先确认问题属于本仓库插件固件还是上游硬件/BSP，避免把衍生功能问题提交给
上游维护者。
