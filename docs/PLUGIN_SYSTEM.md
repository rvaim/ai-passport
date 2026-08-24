# Passport 插件固件 V2.6.0

本文档描述本仓库 `main` 上的插件固件。它是基于
[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport) BSP 演进的社区衍生项目，
目标设备为 ESP32-C3、8 MB Flash、无 PSRAM 的 FoloToy AI Passport。它不是上游官方
插件接口；来源与修改范围见 [`PROJECT_ORIGIN.md`](PROJECT_ORIGIN.md)。

V2 借鉴 Cordis 的 Context、Registry、Service 和生命周期思想，但没有在 MCU 上移植
JavaScript：系统能力和下载包都注册为插件，下载插件运行在固定预算的字节码 VM 中。

面向插件作者的 JSON 字段、完整指令集、权限、字体/UI、签名、测试和发布规范见
[`PLUGIN_DEVELOPMENT_GUIDE.md`](PLUGIN_DEVELOPMENT_GUIDE.md)。本文聚焦固件架构、传输、
存储和整机验收。

## 产品结构

`app_registry` 是唯一应用目录。主页只是 Registry 的一个视图，不再维护另一份菜单：

```text
Registry
  ├─ system.settings 设置：含设备信息；内置兜底，可由同 ID 的签名插件覆盖
  ├─ system.plugins  插件：设备码、Web Bluetooth 安装、第三方插件卸载
  ├─ package.*       plugin_store 中已安装的 app 包
          │
          └─ 固定预算 VM → 宿主服务 → LVGL / 隔离 NVS / 音频 / 定时器 / Nearby
  └─ theme.*         plugin_store 中已安装的纯数据主题包
          └─ ui_theme → 设计 Token → 固件页面 / 语义 UI / 宿主组件
```

每个条目都有 ID、名称、图标、类型、所需服务以及统一的 `enter / exit / key`
生命周期。系统插件声明显示、存储、身份、Nearby、系统设置等服务依赖；下载插件按
Manifest 权限检查服务。不可用的条目仍可见，但不能进入。

`system.settings` 是唯一可替换的保留 ID。安装带 `settings` 权限、Host API 精确为 v5
且 ID 为 `system.settings` 的包后，Registry 仍把设置固定在主页顶部，但进入时运行下载
包；包不可用、启动失败或被移除时自动回到内置设置。`system.plugins` 不能被下载
包覆盖。

主页顶部水平排列“设置”和“插件”两个内置 Registry 条目；下面列出普通下载插件。设备码
和硬件摘要位于设置的“设备”子页。“插件”页始终显示设备码以及 `plugin_store` 中实际
安装的包；真正内置的实现不占包槽，因此不会出现在列表中。安装完成后列表立即重载，长按
返回主页时 Registry 会再次扫描。下载版 `system.settings` 属于 Flash 包，也会显示在这里，
可卸载后回退到内置设置。选择包后用上下键在“否/是”之间切换，并用 OK 确认是否卸载。

下载插件严格只有前台生命周期：Registry 同时最多运行一个 app VM，`plugin_host` 为每次
打开创建新的 owner generation。系统服务的租约、缓冲句柄、Blob 对象、语音、BLE 连接和
异步工作项都绑定该 generation。正常退出、VM fault、start 失败都会调用同一个
`nearby_service_foreground_exit`；插件遗漏 release 不会形成后台任务，迟到事件也不能进入
下一插件。固件没有下载插件开机自启、后台广播或退出后唤醒机制。

## 按键规则

- UP/DOWN 使用 `BUTTON_PRESS_DOWN`，每次物理按下立即产生一次导航事件；不再等待
  180 ms 多击分类，因此快速连续按键不会被合并为一次 double-click。
- 按键驱动把 button 组件连续识别周期设为其支持的最小值 1，不增加应用层防抖。
- OK 短按打开或确认。
- 长按 OK 600 ms 返回上一层：确认框回插件列表，设备信息回设置列表；只有功能顶层的
  上一层才是主页。
- 自动熄屏后，第一下按键只唤醒屏幕；OK 的同一次 click/long 后续事件也会被消费，
  不会在黑屏状态下误触发操作。

三颗按键共享 GPIO0 的 ADC 电阻梯，硬件只能识别某一时刻的一个电压档，无法可靠识别
UP+DOWN 真正同时按下。因此返回使用长按 OK，而不是伪造组合键。

## 设备码

设备码由 ESP32-C3 eFuse 中的 48 bit factory base MAC 确定：把完整 48 bit 数值按固定
10 位 Crockford Base32 编码，再显示为 `XXXXX-XXXXX`。编码没有截断或散列，因此在
48 bit 输入域上是一一映射；设备码的唯一性直接继承工厂 MAC 的唯一性。相同设备每次
启动得到相同编码，不占用 NVS，也不依赖原厂 `cardid` 分区。

设备码只用于让网页和当前 BLE 连接同步，不被当作密码或安全凭证。每次新 BLE 连接都
必须重新提交完整 10 字符编码；断线后同步状态立即失效。同步前的开始、数据、完成、
中止和重置命令都会被拒绝。所有浏览器命令还绑定连接代次，旧连接排队中的命令不会
作用于新连接。

## 无配对近场安装

设备本身不连接 Wi-Fi，也不需要互联网。安装端使用 Chrome 的 Web Bluetooth 直接访问
设备的无配对 BLE GATT 服务；当前固件不包含 SoftAP、HTTP 服务器或 Wi-Fi 安装通道。
不支持 Web Bluetooth 的浏览器不在范围内。

1. 在设备主页打开 **插件**，记下页面顶部的设备码。
2. 从 `web/` 启动本地静态网页：

   ```bash
   cd web
   python3 -m http.server 8000
   ```

3. 用桌面或 Android Chrome 打开 `http://localhost:8000/installer.html`。
4. 输入设备码并点击同步。首次访问时 Chrome 强制显示设备授权列表；选择
   `Passport-XXXX`。同一 HTTPS/localhost 来源再次访问时，网页先用 `getDevices()`
   找回已授权设备并逐个核对设备码，匹配成功就不再显示列表。
5. 同步成功后选择已签名的 `.fpp` 文件并发送。
6. 设备验签后显示名称、作者、版本和权限；在设备上短按 OK 完成安装。

`localhost` 或 HTTPS 是 Web Bluetooth 的浏览器安全上下文要求，不表示设备需要联网。
网页无法取消首次设备选择框，这是 Web Bluetooth 的浏览器权限边界，不是设备端要求。
原生 Android 应用可以自行扫描广播并自动调用 GATT 连接，因此可隐藏设备选择过程；
底层仍会建立短时 BLE 连接，只是不需要配对。用 BLE 广播分片传整个插件既不受网页 API
支持，也缺少可靠传输的确认、重传和流控，所以本固件不采用该方案。
也可使用命令行：

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/send_plugin_ble.py \
  --device-code XXXXX-XXXXX \
  examples/plugins/counter/counter.fpp
```

设备名的 XXXX 是设备码末四位，只帮助在 Chrome 列表中辨认；真正同步仍比较完整编码。

### BLE GATT 协议 V2

| 项目 | UUID |
| --- | --- |
| Service | `f0771000-6f6c-6f74-6f79-70617373706f` |
| Control | `f0771001-6f6c-6f74-6f79-70617373706f` |
| Data | `f0771002-6f6c-6f74-6f79-70617373706f` |
| Status | `f0771003-6f6c-6f74-6f79-70617373706f` |

Control 命令：

- `10 + 10 ASCII chars`：同步设备码；
- `01 + u32_le total_size`：开始接收；
- `02`：接收完成并验签；
- `03`：拒绝/中止；
- `04`：清除完成或错误状态。

Data 是 `u32_le offset + payload`，offset 必须严格连续。Status 固定 20 字节：

```text
version:u8 (=2), installer_state:u8, sync_state:u8, reserved:u8,
error:i32, expected:u32, received:u32, plugin_version:u32
```

`sync_state` 为 0 disconnected、1 connected、2 synced、3 code mismatch。设备端物理批准
没有远程命令。

## 前台 Nearby Runtime Gateway

安装 GATT 与运行期通信共享同一个系统 NimBLE owner，但使用两个固定 profile，绝不同时
运行。离开“插件”页会停止 Installer；前台 app 执行 `nearby_acquire` 后启动 Runtime。
若 Installer 的异步 stop 晚到，它只允许停止 Installer mode，不能误停已经切换到 Runtime
的连接。这一 mode 检查是系统 BLE 仲裁边界。

Runtime UUID：

| 项目 | UUID | 属性 |
| --- | --- | --- |
| Service | `f0772000-6f6c-6f74-6f79-70617373706f` | Primary |
| Control | `f0772001-6f6c-6f74-6f79-70617373706f` | Write，设备码同步 |
| RX | `f0772002-6f6c-6f74-6f79-70617373706f` | Write / Write Without Response，客户端到设备 |
| TX | `f0772003-6f6c-6f74-6f79-70617373706f` | Notify，设备到客户端 |
| Status | `f0772004-6f6c-6f74-6f79-70617373706f` | Read / Notify |

客户端顺序固定为：扫描 Runtime Service → 连接 → 请求 MTU → 订阅 TX → 向 Control 写入
`0x10 + 10 ASCII compact device code` → 等 Status/插件事件变为 synced → 收发。它是无配对
BLE GATT，但仍有短时连接；新连接、断线和 mode 切换都会增加 session generation 并清除同步。
同步前写 RX 返回 insufficient authentication。

Status 固定 8 字节：

```text
version:u8 (=1), sync_state:u8, tx_subscribed:u8, reserved:u8,
session_generation:u32_le
```

RX/TX 上的每个协议帧最多 253 字节，统一 16 字节头，因此 payload 最多 237 字节：

```text
version:u8 (=1), type:u8, flags:u8, reserved:u8 (=0),
id:u32_le, offset_or_sequence:u32_le, total:u32_le, payload...
```

flags：bit 0 `FIRST`、bit 1 `LAST`、bit 2 `ACCEPT`。type：1 MESSAGE、2 BLOB_OFFER、
3 BLOB_DECISION、4 BLOB_DATA、5 BLOB_COMPLETE、6 BLOB_CANCEL、7 VOICE、8 ACK、9 ERROR。

- MESSAGE 按严格 offset 重组，FIRST 的 offset 必须为 0，LAST 时 received 必须等于 total；
  total 最大 4096。完整后才创建 VM buffer handle。
- BLOB_OFFER 的 payload 是 `sha256[32] + name_len:u8 + mime_len:u8 + name + mime`，total
  是文件长度。插件 accept 后系统擦除 `nearby_data`，再回 BLOB_DECISION/ACCEPT；BLOB_DATA
  依次落盘，BLOB_COMPLETE 后核对 SHA-256，一致才产生对象句柄与 ACK。
- 设备发送 Blob 时先 offer，必须等客户端 decision；accept 后分片通知数据，最后 complete。
  系统继续保留源句柄，直到客户端返回大小和摘要都匹配的 ACK 才报告 `BLOB_SENT`；不会把
  GATT notification 入队成功误当作文件校验成功。
- VOICE 每帧是独立 164-byte IMA-ADPCM block：2-byte predictor、1-byte step index、1-byte
  reserved、319 个 4-bit sample。解码为 320 个 16-bit mono sample，即 16 kHz 的 20 ms。

Runtime Gateway 当前只实现 ESP32-C3 的 Peripheral/GATT Server 角色。原生 Android 客户端
可后台扫描特定 service 并自动连接，因此 UI 上不必显示浏览器选择框；Web Bluetooth 仍受
Chrome 授权模型限制。设备到设备直连需要增加 Central 角色与连接仲裁，Wi-Fi/NAN 也尚未
作为本版本传输后端；插件的 `nearby` 权限不会得到任意 NimBLE、Wi-Fi 或 Socket API。

仓库的 [`tools/nearby_client.py`](../tools/nearby_client.py) 是 Bleak 参考实现，覆盖发现、
TX 订阅、设备码同步、双向消息、Blob offer/decision/data/complete/ACK 和摘要校验。它不是
后台产品客户端，也不实现电脑麦克风；语音线格式按上表由原生客户端接入。

系统资源边界：4 个 4096-byte RAM buffer、一个 768 KiB Flash Blob 对象、一个 Blob 流、
一个半双工语音会话。GATT callback 只复制有界帧到队列；Flash、SHA、VM 和音频工作不在
NimBLE callback 中执行。UI 线程每 20 ms 最多派发 4 个 Nearby 事件，保持 VM 串行语义。

## 插件包、UI 服务与运行时

V2.6.0 只接受一套当前格式：package v1、magic `PLG5` 的 Manifest v5、app 字节码 v1、
Host API v5。没有版本范围、旧 Manifest 解析或插件格式迁移分支：

```text
.fpp
  ├─ 固定头：版本、内容长度、SHA-256、ECDSA P-256 签名
  └─ 已签名内容
      ├─ Manifest：ID、名称、作者、版本、权限、kind、事件入口
      ├─ app：V1 字节码 + UTF-8 字符串表
      └─ theme：固定 64-byte THM1 Token payload
```

安装顺序为完整接收、摘要/签名验证、设备端确认，然后写入插件槽。下载插件不能取得
任意指针，不能创建 FreeRTOS 任务，也不能直接调用 ESP-IDF、LVGL、NVS、Flash 或
硬件驱动，只能调用固件提供的宿主接口。

资源上限：

- VM 栈 16 个 `int32_t`，状态槽 16 个，每次事件最多 512 条指令；
- 最多 24 个存活 UI 对象，UI 文本最多 128 UTF-8 字节；
- 每次事件最多 8 次 KV 操作，每插件最多 8 个键，运行期最多提交 128 次写入；
- 最多 4 个定时器，范围 100 ms 至 1 小时；
- 音频 20–10000 Hz，单次最长 1000 ms，并在独立工作任务中播放；
- 4 个系统 RAM buffer，每个最多 4096 字节；一个 768 KiB Nearby Flash 对象；
- Nearby 语音固定 16 kHz/16-bit/mono、20 ms IMA-ADPCM、半双工；
- 插件没有私有堆，也没有声明堆大小的 Manifest 字段。

Manifest 使用连续的 11 项 handler 表：`start`、`up`、`down`、`ok`、`timer0` 至 `timer3`、
`back`、`action`、`nearby`。没有 `back` handler 时长按 OK 直接返回主页。当前 Host API 提供：

- 栈、状态、整数运算、跳转、tone、隔离 KV、定时器和 exit；
- `setting_load / setting_save / device_info`：复用全局设置和统一设备信息页；
- 插件源码模板和 `["include", "name"]` 编译期展开；
- `ui_screen / ui_value_card / ui_list_row`：宿主布局的语义 UI；
- `ui_action_bar`：语义 UI 和自由绘制都可复用的底部按键提示；
- `ui_dialog_confirm` + `action` + `event_load`：非阻塞统一确认弹窗；
- `theme_next / theme_color` 和 setting ID 4；
- Canvas 颜色参数的 `theme:token` 引用；
- 纯数据 theme 包及运行时主题管理。
- 系统 buffer/object handle、Nearby acquire/release、消息、Blob 与半双工语音；
- `event_load type/id/handle/value` 的统一异步事件合同。

语义 UI 不是强制渲染路径。普通列表/设置可完全由宿主组件绘制，游戏可继续使用
`ui_clear/ui_rect/ui_text` 自由绘制，也可像 Meteor Tap 一样只复用动作栏形成混合模式。
宿主主题自动作用于自带页面、语义组件、动作栏和弹窗；Canvas 只有显式使用
`theme:token` 的颜色才跟随主题。

所有插件都必须由当前 `plugin_tool.py` 重新打包。完整格式以
`examples/plugins/counter/plugin.json`、`examples/plugins/settings/plugin.json`、
`examples/plugins/nearby-demo/plugin.json` 和
`tools/plugin_tool.py` 的校验为准。

所有下载插件的 `ui_text`/`ui_state` 字号统一为 14 px。固件常驻 GB2312 的 6,763 个汉字、
常用标点符号和完整 LVGL 插件图标集；打包工具与设备验签阶段使用同一份字符定义检查
Manifest 名称、作者和完整 UTF-8 字符串表。GB2312 之外的生僻字会在打包或安装时明确
拒绝，不会安装后才显示方块。18 px 字体仅供固件自带页面使用，并按源码自动裁剪。

系统设置由 `device_settings` 服务统一持有并异步写入 NVS：亮度 10–100%、音量
0–100%、按键音开关、自动熄屏 Never/30 秒/1 分钟/3 分钟/5 分钟；当前主题由
`ui_theme` 以稳定 ID 单独持久化。亮度直接更新 LEDC；
codec 音量和按键音在工作任务中处理，不阻塞按键采样或 LVGL；快速修改时长度为 1 的
快照队列始终保留最新完整设置。

### 当前存储 schema

本固件不读取测试阶段的旧数据：插件槽提交头使用 `PCS4`，系统设置命名空间为
`pass_sys_v4`，主题命名空间为 `pass_ui_v4`，插件 KV 用带 `passport-kv-v4:` 盐值的新命名
空间派生算法。旧槽和旧 NVS key 即使仍留在 Flash 也不会进入 Registry 或当前运行时；首次
启动等同于全新设置、无已安装插件和空插件 KV。这里没有迁移、复制、字段猜测或回退读取。

## 创建和签名示例插件

首次创建开发密钥：

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/plugin_tool.py keygen \
  --private .keys/plugin-signing-private.pem \
  --public .keys/plugin-signing-public.pem \
  --header components/plugin_runtime/private/plugin_trust_key.h
```

打包并检查：

```bash
python3 tools/plugin_tool.py pack examples/plugins/counter/plugin.json \
  --private .keys/plugin-signing-private.pem \
  --output examples/plugins/counter/counter.fpp

python3 tools/plugin_tool.py inspect examples/plugins/counter/counter.fpp \
  --public .keys/plugin-signing-public.pem
```

可替换 Settings 包使用同样命令打包，源文件位于
`examples/plugins/settings/plugin.json`，发布包内文件名为 `settings.fpp`。
主题包使用同一签名流程；参考
`examples/plugins/midnight-theme/plugin.json`，输出为 `midnight-theme.fpp`。主题包不进入
VM，但仍通过同一包摘要、签名、字符集、设备批准和原子存储流程。

## Flash 布局

| 名称 | 偏移 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| nvs | `0x9000` | 24 KiB | 系统设置和插件 KV |
| factory | `0x10000` | 3 MiB | 固件 |
| imgstore | `0x310000` | 128 KiB | 原资源分区，保留 |
| imgframe | `0x330000` | 152 KiB | 原资源分区，保留 |
| cardid | `0x356000` | 16 KiB | 原身份分区，保留 |
| audio | `0x37a000` | 512 KiB | 原资源分区，保留 |
| imgava | `0x3fa000` | 1 MiB | 原资源分区，保留 |
| plugin_store | `0x4fa000` | 2 MiB | 8 个 256 KiB 插件槽 |
| plugin_stage | `0x6fa000` | 256 KiB | 接收和验签暂存 |
| nearby_data | `0x73a000` | 768 KiB | 当前前台插件的单个临时 Blob 对象 |

单个 `.fpp` 上限 252 KiB，app 与 theme 合计最多 7 个活动包，始终留一个空槽。更新使用“写空槽 →
重新验签 → 最后提交魔数 → 擦旧槽”的顺序；失败不会修改当前活动插件。

V2.6.0 不解析 PLG4/Host API 4 包；请用本版本随附的 `.fpp` 重新安装。无效旧槽不计入活动
插件数，安装新包时会按空槽覆盖。`nearby_data` 不是持久化插件文件系统，重启或下一次接收
都不承诺保留其对象元数据。

## 构建、测试与刷写

```bash
. /path/to/esp-idf-v5.5.3/export.sh
tests/run_host_tests.sh
idf.py build
idf.py size
idf.py -p PORT flash
```

`idf.py flash` 分段写入 bootloader、分区表和 factory 应用，不主动写 NVS、cardid、资源
或插件数据分区。发布目录中的 `factory-merged.bin` 从 `0x0` 写入，适合空片或完整重装；
它包含启动镜像、分区表和应用，但写入方式应由使用者明确选择。

## 实机验收

构建通过不等于硬件通过。刷写后至少验证：

1. USB 日志无重启循环、断言或看门狗；
2. 连续快速按 UP/DOWN，每次按下均移动一次；
3. 主页顶部仅显示“设置”和“插件”，下载插件列表和不可用状态显示正常；
4. 长按 OK 逐层返回：弹窗 → 列表、设备信息 → 设置、功能顶层 → 主页；
5. 错误设备码不能发送，正确码在断线重连后必须重新同步；
6. Chrome Web Bluetooth 可连续安装、断开并重新进入“插件”；
7. 安装 Counter 后主页出现它，重启后计数仍在；
8. 设置中的设备子页显示固定设备码、芯片、Flash 和可用内存；
9. 安装 Host API 5 `settings.fpp` 后“设置”位置不变，五项设置和设备子页均可用；
10. 选择任一已安装包时确认层默认“否”，上下切换，OK 选“是”后包从 Flash 中消失；
11. 设置 30 秒熄屏，确认首个 UP/DOWN/OK 只唤醒且不会改变选择或进入页面；
12. 同一浏览器来源第二次同步已授权设备时不再出现选择框；
13. 反复进入/退出插件页，日志无持续堆下降；
14. 待批准、提交和更新过程中断电后，旧插件或新插件至少有一个仍完整可用；
15. Counter 的确认弹窗默认取消，上下切换后 OK 才清零，完成后 action 只派发一次；
16. 安装 Midnight Theme 后自带页面、Settings、Counter 与 Meteor 的宿主动作栏统一换肤；
17. 卸载当前主题后立即回退“像素原野”，重启仍能正常进入所有页面。
18. 安装 Nearby Demo，原生客户端订阅 TX 后用错误/正确设备码分别验证拒绝与 synced；
19. 双向消息、Blob accept/reject/摘要校验和语音收发事件均能按 ID 对应；
20. 在发送、接收、录音中退出插件，日志出现 foreground resources released，BLE 停止且音频锁释放；
21. 故意触发 VM fault 后打开其他插件，旧连接、句柄、声音或事件不会继续存在。
