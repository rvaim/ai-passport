# Passport 可安装插件开发指南与规范

本文面向编写 `.fpp` 下载插件的开发者，适用于 Passport 插件固件 V2.6.0：
Manifest v5、字节码 v1、Host API v5。本文既是快速上手教程，也是当前实现的行为规范。
固件整体架构、BLE 协议、Flash 分区和实机验收见
[`PLUGIN_SYSTEM.md`](PLUGIN_SYSTEM.md)。

本规范属于 `rvaim/ai-passport` 衍生固件，不是上游
[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport) 的通用插件标准。项目来源与
兼容边界见 [`PROJECT_ORIGIN.md`](PROJECT_ORIGIN.md)。

> 这里的“插件”不是 ESP-IDF 原生代码，也不是动态链接库。插件源码是受约束的 JSON，
> 经工具编译成固定字节码、签名为 `.fpp`，再由设备上的有界 VM 执行。插件不能获得任意
> 指针，也不能直接调用 LVGL、FreeRTOS、ESP-IDF、NVS 或硬件驱动。

## 1. 五分钟创建第一个插件

### 1.1 安装工具依赖

在仓库根目录执行：

```bash
python3 -m pip install -r tools/requirements.txt
```

打包需要 `cryptography`，命令行 BLE 安装额外需要 `bleak`。

### 1.2 创建源码目录

推荐每个插件使用以下结构：

```text
examples/plugins/hello/
├── plugin.json        # 人工维护的唯一源码
└── hello.fpp          # 生成物，不手工修改
```

推荐使用 Host API 5 的语义 UI。最小可运行的 `plugin.json`：

```json
{
  "id": "dev.example.hello",
  "name": "你好",
  "author": "Example",
  "version": 1,
  "host_api": 5,
  "permissions": [],
  "state_slots": 1,
  "handlers": {
    "start": [
      ["ui_screen", "你好"],
      ["ui_value_card", "第一个插件", "integer", 0, ""],
      ["ui_action_bar", "调整", "确认", "返回"],
      ["ui_commit"],
      ["end"]
    }
  }
}
```

没有 `back` handler 时，长按 OK 由固件直接退出插件并回到主页。简单的单页插件因此不必
自行处理返回。语义 UI 不是强制要求；需要游戏、动画或独立视觉的插件仍可使用坐标绘制，
也可以只复用宿主动作栏、弹窗和主题颜色，详见第 7 节。

### 1.3 准备签名密钥

设备只接受与固件内置公钥匹配的签名。若仓库已有
`.keys/plugin-signing-private.pem`，应使用这把匹配当前固件的私钥；不要重新生成。

若是新建一套固件和信任根，可生成 P-256 密钥：

```bash
python3 tools/plugin_tool.py keygen \
  --private .keys/plugin-signing-private.pem \
  --public .keys/plugin-signing-public.pem \
  --header components/plugin_runtime/private/plugin_trust_key.h
```

随后必须重新构建并刷入包含新 `plugin_trust_key.h` 的固件。只生成新私钥、却不重刷固件，
设备必然拒绝该私钥签出的包。私钥默认应保存在被 `.gitignore` 忽略的 `.keys/` 中。

### 1.4 打包和检查

```bash
python3 tools/plugin_tool.py pack \
  examples/plugins/hello/plugin.json \
  --private .keys/plugin-signing-private.pem \
  --output examples/plugins/hello/hello.fpp

python3 tools/plugin_tool.py inspect \
  examples/plugins/hello/hello.fpp \
  --public .keys/plugin-signing-public.pem
```

`pack` 会同时检查字段、事件、指令参数、Host API、权限、UI 边界、字体字符和包大小。
`inspect` 会检查包结构和摘要；提供 `--public` 时还会验证签名。

### 1.5 安装到设备

1. 在设备主页进入“插件”，记下顶部设备码，并保持该页打开。
2. 在 `web/` 启动本地服务器：

   ```bash
   python3 -m http.server 8000 --directory web
   ```

3. 用桌面或 Android Chrome 打开 `http://localhost:8000/installer.html`。
4. 输入设备码、选择设备、选择 `.fpp` 并发送。
5. 设备完成验签后会显示包信息；在设备上短按 OK 批准安装。

也可以使用命令行：

```bash
python3 tools/send_plugin_ble.py \
  --device-code XXXXX-XXXXX \
  examples/plugins/hello/hello.fpp
```

BLE 不需要系统配对，但仍会建立临时 GATT 连接。网页首次访问某台设备时，Chrome 必须
显示浏览器自己的授权选择框；同一来源后续可通过已授权设备自动重连。

## 2. 运行模型

```text
主页 Registry
  └─ 打开下载插件
       ├─ 映射并再次验签 Flash 中的包
       ├─ 创建唯一前台 owner，VM 的 16 个以内状态槽清零
       ├─ 创建插件 screen
       ├─ 派发 start
       ├─ 串行派发 UP / DOWN / OK / timer0..3 / back / action / nearby
       └─ 退出时统一撤销 Nearby/音频/缓冲区/传输/定时器，再删除 screen、解除映射
```

theme 包不进入这条 VM 生命周期；安装后由 `ui_theme` 解析固定 payload，并作为设计 Token
来源供固件与 app 宿主组件读取。

每次事件处理都有一份新的空操作数栈；栈不会跨事件保存。`state_slots` 是插件打开期间的
内存状态，跨事件保留，但退出再进入后重新清零。需要跨重启或跨打开保存的数据必须使用
`kv_load` / `kv_save`。

一个事件最多执行 512 条字节码指令，操作数栈最多 16 个 `int32_t`。越界、除零、权限
不足、错误跳转、无效字符串或宿主调用失败都会使插件进入“插件错误”页，并停止其定时器
与待播放音效。长按 OK 可离开错误页。

VM、按键和 LVGL 调用均由固件串行化。插件不能创建线程，也不需要处理 VM 内部的数据竞争。
音效播放、BLE 收发、Blob Flash I/O 和语音编解码由宿主工作任务异步完成。

所有下载插件只能前台运行。系统只有一个活动 VM 和一个前台 owner，不提供后台 task、后台
广播、后台回调、开机自启或退出后唤醒入口。插件主动调用 `nearby_release`、
`nearby_voice_stop`、`buffer_release` 是良好习惯，但不是资源安全的前提：正常退出、长按返回、
VM fault、start handler 失败都会进入同一宿主清理路径，系统按 owner 代次强制取消传输、停止
音频和 BLE、使全部句柄失效。旧代次的迟到队列项不会交给下一个插件。

## 3. 顶层 JSON 规范

打包器会拒绝未知顶层字段，防止把拼写错误静默当成默认值。允许的字段如下：

| 字段 | 必填 | 类型与范围 | 说明 |
| --- | --- | --- | --- |
| `id` | 是 | 非空 ASCII，最多 31 字节 | 只允许字母、数字、`_`、`-`、`.`；永久身份 |
| `name` | 是 | 非空 UTF-8，最多 47 字节 | 主页与批准页显示；必须属于公共字库 |
| `author` | 是 | 非空 UTF-8，最多 31 字节 | 批准页显示；必须属于公共字库 |
| `version` | 是 | `1..4294967295` | 同 ID 更新时不得低于已安装版本 |
| `kind` | 否 | `app` 或 `theme`，默认 `app` | 可执行应用或纯数据主题包 |
| `host_api` | 是 | 必须为 `5` | 精确声明当前 Host API |
| `permissions` | 否 | 字符串数组，默认 `[]` | 可用值见权限章节；规范建议始终显式写出 |
| `state_slots` | 否 | `0..16`，默认 0 | 运行期 `int32_t` 状态槽数量 |
| `templates` | 否 | 对象 | 可复用的指令数组，只在打包时展开 |
| `handlers` | app 必填 | 对象 | 至少包含一个受支持的事件 handler |
| `theme` | theme 必填 | 对象 | 主题 Token、面板样式和背景装饰 |

打包器只接受显式的 `"host_api": 5`，不提供默认值或版本范围。主题包同样固定要求
Host API 5，且不能声明权限、状态槽、模板或 handlers。

### 3.1 ID 与版本规则

- 普通插件推荐使用反向域名：`dev.company.feature`。
- ID 是更新、KV 隔离和 Registry 身份的依据；发布后不要修改。
- 相同 ID 的新包会更新原插件。更高版本和相同版本允许安装，低版本拒绝。
- `system.` 命名空间由固件保留。当前唯一允许的下载覆盖项是 `system.settings`。
- `system.plugins` 不可替换；打包器会拒绝该 ID。
- 卸载只移除 Flash 中的包，不清除其隔离 KV。用相同 ID 重装时可继续读到旧数据。

### 3.2 模板和标签

模板用于消除重复的整屏渲染代码：

```json
{
  "templates": {
    "render": [
      ["ui_clear", "#F4EBD8"],
      ["ui_title", "示例"],
      ["ui_state", 120, 130, 0, "center", "#17202A", "值：", 0],
      ["ui_commit"],
      ["end"]
    ]
  },
  "handlers": {
    "start": [["include", "render"]]
  }
}
```

`["include", "render"]` 是编译期伪指令，不会进入包。递归 include 会被拒绝。`label`
也是伪指令，作用域只在一个展开后的 handler 内；同一 handler 中重复 include 含同名标签
的模板会造成重复标签错误。

## 4. 事件与按键规范

| Handler | 触发时机 | 建议用途 |
| --- | --- | --- |
| `start` | 每次打开插件，VM 初始化后一次 | 初始化状态、读取 KV、创建首屏、启动定时器 |
| `up` | UP 每次物理按下 | 向上导航、数值变化、游戏输入 |
| `down` | DOWN 每次物理按下 | 向下导航、数值变化、游戏输入 |
| `ok` | OK 短按完成一次 click | 打开、确认、主要动作 |
| `timer0`…`timer3` | 对应定时器到期 | 动画、时钟、游戏 tick |
| `back` | OK 长按 600 ms | 返回插件内上一层，或请求退出 |
| `action` | 宿主异步控件完成 | 接收弹窗等宿主组件的 ID 和结果 |
| `nearby` | 系统近场状态或异步传输事件 | 接收连接、消息、Blob、语音和错误事件 |

UP/DOWN 消费物理 `PRESS`，不等待多击分类，所以快速连续按下会逐次派发。插件不要自行增加
防抖或等待双击。OK 短按和长按由固件区分。

返回规则：

- 没有 `back`：长按 OK 直接回主页。
- 有 `back`：固件先执行 handler；执行 `exit` 才回主页。
- 多层插件应在子页收到 `back` 时恢复上一页并 `end`；在顶层才执行 `exit`。
- `exit` 只是向产品壳请求退出，不代表 VM 中的后续指令自动停止；通常紧接 `end`。

`action` 是 Host API 5 的异步结果事件。宿主不会在调用 `ui_dialog_confirm` 的原 handler
里阻塞等待按键；用户完成选择后，宿主关闭弹窗，再派发一次 `action`。使用
`["event_load", "id", slot]` 和 `["event_load", "value", slot]` 把事件数据写入状态槽。
确认弹窗的 `value` 为 `0`（取消）或 `1`（确认），`id` 是创建弹窗时传入的 ID。长按 OK
视为取消。没有 `action` handler 时结果被安全丢弃。

Host API 5 的所有异步事件共享四个字段：`type`、`id`、`handle`、`value`。`action` 只使用
`id/value`，其余两项为 0；`nearby` 使用完整四项。读取方式相同：

```json
["event_load", "type", 0]
["event_load", "id", 1]
["event_load", "handle", 2]
["event_load", "value", 3]
```

字段只在当前 handler 内代表本次事件；需要跨事件保留时必须复制到插件自己的状态槽。
`handle` 是宿主不透明句柄，不是地址，不能做算术或跨插件保存。

典型两层返回：

```json
"back": [
  ["load_state", 1],
  ["jz", "leave_plugin"],
  ["push", 0],
  ["store_state", 1],
  ["include", "render_list"],
  ["label", "leave_plugin"],
  ["exit"],
  ["end"]
]
```

## 5. 指令通用格式

每条指令都是 JSON 数组，第一个元素为操作名，后面是有序操作数：

```json
["push", 10]
["ui_text", 120, 100, 0, "center", "#17202A", "文本"]
```

整数必须是 JSON 整数，不能用浮点数或布尔值代替。颜色可写 `"#RRGGBB"`、
`0..0xFFFFFF` 的整数，Host API 5 Canvas 也可写 `"theme:token"`。打包器在每个 handler
末尾缺少 `end` 时会自动追加，但规范建议显式写出，便于审查控制流。

## 6. 栈、状态和控制流指令

下表中的“栈效果”以右侧为栈顶；`a b → c` 表示先弹出 `b`，再弹出 `a`，压入 `c`。

| 指令 | 栈效果 | 行为 |
| --- | --- | --- |
| `["end"]` | 不要求为空 | 正常结束当前事件 |
| `["push", value]` | `→ value` | 压入有符号 32 位整数 |
| `["load_state", slot]` | `→ state[slot]` | 读取状态槽 |
| `["store_state", slot]` | `value →` | 写状态槽 |
| `["event_load", "type"|"id"|"handle"|"value", slot]` | 无 | Host API 5；把当前异步事件字段写入状态槽 |
| `["add"]` | `a b → a+b` | 32 位补码回绕加法 |
| `["sub"]` | `a b → a-b` | 32 位补码回绕减法 |
| `["mul"]` | `a b → a*b` | 32 位补码回绕乘法 |
| `["div"]` | `a b → a/b` | 有符号除法，向零截断；`b=0` 报错 |
| `["mod"]` | `a b → a%b` | 有符号余数；`b=0` 报错 |
| `["eq"]` | `a b → 0/1` | 相等比较 |
| `["lt"]` | `a b → 0/1` | `a < b` |
| `["gt"]` | `a b → 0/1` | `a > b` |
| `["not"]` | `a → 0/1` | `a==0` 得 1，否则得 0 |
| `["dup"]` | `a → a a` | 复制栈顶 |
| `["drop"]` | `a →` | 丢弃栈顶 |
| `["label", name]` | 无 | 编译期标签，不占指令预算 |
| `["jump", label]` | 无 | 无条件跳转 |
| `["jz", label]` | `condition →` | 条件为 0 时跳转 |
| `["jnz", label]` | `condition →` | 条件非 0 时跳转 |

标签跳转使用有符号 16 位相对偏移，目标必须在同一 handler 的代码范围内。循环必须保证
所有路径在 512 条执行预算内结束。打包器会检查标签存在和偏移范围，但运行路径预算由 VM
最终执行时验证。

## 7. UI 模型与复用规范

Host API 5 提供三种同等受支持的 UI 模式。插件不被强制使用宿主组件，选择取决于产品需求：

| 模式 | 适用场景 | 主题行为 | 典型示例 |
| --- | --- | --- | --- |
| 语义 UI | 设置、列表、表单、普通工具 | 宿主自动应用当前主题和布局 | Settings、Counter |
| 自由绘制 Canvas | 游戏、动画、品牌化或像素级布局 | 插件控制画面；可主动读取主题色 | Meteor Tap 游戏主体 |
| 混合模式 | 自绘主体，同时复用系统交互 | 自绘区由插件控制；动作栏和弹窗自动跟随主题 | Meteor Tap |

三种模式可以在同一个插件中按页面切换，但同一帧必须先选定基础模式：以 `ui_screen` 开始
的是语义帧，以 `ui_clear` 开始的是自由绘制帧。随后调用 `ui_action_bar` 或
`ui_dialog_confirm` 不会改变基础模式。所有帧最后都用 `ui_commit` 提交。

### 7.1 语义 UI

语义 UI 描述“这是什么”，而不是“画在哪里”。宿主负责状态栏、电量、间距、面板、选中态、
滚动窗口、底部动作栏以及主题适配。推荐的构建顺序是：

```json
[
  ["ui_screen", "设置"],
  ["ui_list_row", 0, "\uf06e", "亮度", "percent", 1, 0, true],
  ["ui_list_row", 1, "\uf028", "音量", "percent", 2, 0, true],
  ["ui_action_bar", "选择", "修改", "返回"],
  ["ui_commit"],
  ["end"]
]
```

| 指令 | JSON 参数 | 合同 |
| --- | --- | --- |
| `ui_screen` | `title` | 开始新的语义帧并清空上一次语义描述；标题最多 48 字节 |
| `ui_value_card` | `label, kind, state_slot, suffix` | 添加一个大数值卡片；一帧最多一个，suffix 最多 24 字节 |
| `ui_list_row` | `row_id, icon, label, kind, value, selected_slot, enabled` | 添加或覆盖一行；row ID `0..7`，最多 8 行 |
| `ui_action_bar` | `navigation, ok, back` | 统一三段底部动作提示；每段最多 24 字节，可传空字符串 |
| `ui_commit` | 无 | 根据完整描述重建 screen；超过 5 行时自动让选中项保持可见 |

值类型 `kind`：

| kind | `value` 参数 | 显示规则 |
| --- | --- | --- |
| `none` | 字符串，通常 `""` | 不显示右侧值 |
| `text` | 固定字符串 | 原样显示 |
| `integer` | 状态槽 | 十进制整数；value card 可追加 suffix |
| `percent` | 状态槽 | `数值%` |
| `toggle` | 状态槽 | 0 显示“关闭”，非 0 显示“开启” |
| `duration` | 状态槽 | 0 显示“从不”，小于 60 显示秒，否则显示分钟 |
| `theme` | 状态槽 | 把主题索引解析为主题名称 |

`ui_value_card` 只接受 `integer`、`percent`、`toggle`、`duration` 或 `theme`。
`ui_list_row` 的 `selected_slot` 指向一个保存当前行 ID 的状态槽；宿主据此绘制选中态和计算
五行可视窗口。`enabled=false` 只改变视觉状态，不会替插件拦截 OK，handler 仍必须根据自身
状态决定是否执行动作。语义组件本身不产生事件；物理键仍派发 `up/down/ok/back`。

### 7.2 自由绘制 Canvas

设备逻辑分辨率为 240 × 320，左上角是 `(0, 0)`。自由绘制使用即时式整屏重建：先
`ui_clear`，按背景到前景创建对象，最后 `ui_commit`。

| 指令 | 参数 | 约束与行为 |
| --- | --- | --- |
| `ui_clear` | `color` | 创建或清空自由绘制 screen，设置背景并重置对象计数 |
| `ui_title` | `text` | 绘制宿主状态栏和电量；标题最多 48 UTF-8 字节 |
| `ui_text` | `x,y,font,align,color,text` | 静态文本；最多 128 UTF-8 字节 |
| `ui_state` | `x,y,font,align,color,prefix,slot` | 前缀加状态槽十进制值；前缀最多 64 字节 |
| `ui_rect` | `x,y,width,height,color` | 无圆角、无边框的实色矩形 |
| `ui_commit` | 无 | 请求 LVGL 刷新当前 screen |

坐标与预算：

- `font` 当前必须为 `0`，固定映射到公共 14 px 字体。
- `align` 只能是 `left`、`center`、`right`；左对齐 x 为 `-40..240`，右对齐 x 为
  `0..280`，居中 x 是中心锚点，常用 `120`。
- 文字 y 为 `-40..320`；矩形 x 不小于 `-240`、y 不小于 `-320`，宽 `1..480`、高
  `1..640`。允许部分离屏，但不允许宿主必然拒绝的几何值。
- 一次 `ui_clear` 后最多 24 个 `ui_title/ui_text/ui_state/ui_rect` 请求。每帧完整重绘，
  背景矩形先创建、文字后创建，避免后创建的色块遮住文字。
- 自由绘制不会替换插件写死的 `#RRGGBB`。要跟随主题，可在 `ui_clear`、`ui_text`、
  `ui_state`、`ui_rect` 的颜色位置写 `"theme:token"`，例如
  `["ui_clear", "theme:background"]`。宿主在每次绘制时解析当前 Token，因此切换主题后重绘
  即可生效。
- ESP32-C3 无 PSRAM，动画应控制刷新频率和对象数。

### 7.3 混合模式、动作栏和弹窗

`ui_action_bar` 可用于语义 screen，也可在 `ui_clear` 后叠加到自绘 screen。宿主固定显示
三段提示：`↑↓ + navigation`、`OK + ok`、`长按 + back`，并占用底部 `y=294..319`。插件
主体应避开该区域，不要再绘制一套按键提示。

确认弹窗是宿主管理的模态组件：

```json
["ui_dialog_confirm", 7, "确认删除?", "此操作无法撤销", "取消", "删除"]
```

- dialog ID 范围 `1..65535`，由插件分配，用于区分多个异步请求。
- 标题最多 48 字节、消息最多 96 字节、两个按钮各最多 24 字节。
- 默认选择取消；UP/DOWN 在取消和确认之间切换，短按 OK 提交，长按 OK 取消。
- 弹窗打开期间宿主消费导航键，不会同时派发插件的 `up/down/ok/back`。
- 完成后派发 `action(id, value)`；插件必须在 `action` handler 中处理结果并按需重绘。
- 同一时刻只能打开一个弹窗。在已有弹窗时再次调用会产生 Host error。

动作栏、弹窗、语义行、数值卡片，以及固件自带主页、设置页、插件管理页共用同一套 UI
组件和主题 Token。主题切换后，下一次语义重绘或重新进入页面就会使用新主题；插件无需复制
圆角、边框、阴影、选中态或底部布局。

### 7.4 主题 Token API

Canvas 或混合插件可以读取当前主题色：

```json
["theme_color", "text", 0]
```

指令把 `#RRGGBB` 作为 `0x00RRGGBB` 整数写入状态槽，不需要权限，但要求 Host API 5。
可用 Token：`background`、`surface`、`text`、`text_muted`、`accent`、`accent_strong`、
`selection`、`muted_surface`、`danger`、`success`、`border`、`selection_border`。

Canvas 绘制通常无需先调用 `theme_color`，直接在颜色参数中写 `"theme:background"`、
`"theme:text"` 等即可。`theme_color` 用于确实需要在 VM 中读取色值做整数计算或状态判断的
场景。无论哪种方式，都只影响下一次绘制；已经创建的对象不会原地换色，主题变化后应完整
重绘当前帧。

### 7.5 公共 14 px 字体

所有可安装 app 和 theme 包的显示字符串统一接受公共 14 px 字库检查，包含：

- ASCII `0x20..0x7E`；
- GB2312 的 7,444 个非 ASCII 字符（其中 6,763 个汉字及常用符号）；
- LVGL `lv_symbol_def.h` 中的 60 个内置图标码点。

Manifest 名称、作者和 app 的完整字符串表在打包时检查，设备验签时用同一字符范围再次检查。
不在范围内的字符会明确报告 Unicode 码点，不会等安装后才显示方块。字符串可换行，但
Manifest 的 `name/author` 不允许控制字符或换行。

图标可用 JSON Unicode 转义，例如 `"\uf028"`。完整码点以
`managed_components/lvgl__lvgl/src/font/lv_symbol_def.h` 为准。不要使用 emoji、GB2312 外
生僻字或任意私用区图标。扩展字库时必须同时更新 `tools/ui_charset.py`、重新生成字体和设备
端字符表，并重新评估固件 Flash；插件包不能携带自己的字体。

## 8. 宿主能力指令

### 8.1 音效

```json
["tone", 880, 35]
```

- 需要 `audio` 权限。
- 频率 `20..10000 Hz`，时长 `1..1000 ms`。
- 方波由独立工作任务播放，不阻塞 VM。
- 队列深度为 4；一个 handler 通常只应提交一个音效，避免队列满导致宿主错误。
- 实际音量使用设备全局“音量”设置。
- `tone` 不提供 PCM。麦克风只通过 8.6 节固定格式的系统语音流开放，插件不能读取原始 PCM。

### 8.2 隔离 KV

```json
["kv_load", 0, "score", 0]
["kv_save", 0, "score"]
```

- 需要 `storage` 权限。
- `kv_load` 参数为目标状态槽、键、找不到时的回退值。
- `kv_save` 把指定状态槽当前值写入键。
- 只支持有符号 32 位整数。
- 键为 1–15 字节可打印 ASCII，不能包含空格或控制字符。
- 每个插件最多 8 个键；每次事件最多 8 次 KV 操作。
- 每次打开插件最多实际提交 128 次写入；写入与已有值相同不会重复提交 Flash。
- 命名空间由完整插件 ID 派生，插件之间不能读写彼此数据。
- 高频游戏状态应保存在 `state_slots`，仅在关键检查点或最高分变化时写 KV，避免磨损 NVS。

### 8.3 定时器

```json
["timer_set", 0, 1000, true]
```

- timer ID 为 `0..3`，对应 `timer0..timer3` handler。
- 延迟为 `100..3600000 ms`。
- `repeat` 必须是 JSON 布尔值 `true` 或 `false`。
- 对同一 ID 再次 `timer_set` 会替换旧定时器。
- `false` 表示单次定时器；当前没有单独 cancel 指令，可用同 ID 重设，退出插件会统一删除。
- timer handler 与按键 handler 共用 512 指令预算和状态槽。

### 8.4 全局设置

```json
["setting_load", 0, 1]
["setting_save", 0, 1]
```

参数是 setting ID 和状态槽。两条指令都需要 `settings` 权限。

| ID | 设置 | `setting_save` 接受值 |
| ---: | --- | --- |
| 0 | 屏幕亮度 | `10,20,...,100` |
| 1 | 输出音量 | `0,10,...,100` |
| 2 | 按键音 | `0` 关闭，`1` 开启 |
| 3 | 自动息屏秒数 | `0,30,60,180,300`；0 表示从不 |
| 4 | 当前主题索引 | `0..theme_count-1`；索引随已安装主题集合变化 |

写入非法值会产生宿主错误，而不是自动裁剪。设置由固件统一持有并写入 NVS；插件不应再用
自己的 KV 复制一份全局设置。对“切换到下一个主题”这一常见动作，
优先使用：

```json
["theme_next", 5]
```

它需要 `settings` 权限，切换成功后把新索引写入指定状态槽，并持久化选择。
相比插件自己猜测主题数量，这个操作不会产生越界索引。

`["device_info"]` 需要 `settings` 权限，由固件绘制统一的设备码、芯片、
Flash 和内存摘要。该指令会用固件 screen 替换插件当前 screen。返回设置列表时应重新执行
自己的完整 render 模板。

### 8.5 不透明缓冲区与对象句柄

VM 状态槽只能保存 `int32_t`，不能存指针或任意字节。Host API 5 因此提供系统托管的句柄：

```json
["buffer_alloc", 128, 0]
["buffer_append_text", 0, "hello"]
["buffer_length", 0, 1]
["buffer_write_u8", 0, 2, 3]
["buffer_read_u8", 0, 2, 4]
["buffer_release", 0]
```

| 指令 | 参数 | 行为 |
| --- | --- | --- |
| `buffer_alloc` | `capacity, handle_dest` | 分配 1–4096 字节 RAM 缓冲区，把句柄写入状态槽 |
| `buffer_append_text` | `handle_slot, text` | 追加字符串的 UTF-8 原始字节，不追加 NUL |
| `buffer_length` | `handle_slot, length_dest` | 读取当前数据长度；也适用于收到的 Blob 对象 |
| `buffer_write_u8` | `handle_slot, index_slot, value_slot` | 写入 `0..255`，写到当前末尾之后会扩展长度 |
| `buffer_read_u8` | `handle_slot, index_slot, value_dest` | 读取一个无符号字节；也适用于只读 Blob 对象 |
| `buffer_release` | `handle_slot` | 放弃句柄并把该状态槽清零；RAM 与 Blob 对象都适用 |

每个前台插件最多同时持有 4 个 RAM 缓冲区，总上限 16 KiB。收到的小消息和 Blob 元数据也
占用这 4 个槽；插件处理完事件后应尽快 `buffer_release`。发送任务会在内部保留数据，插件
可以提前 release，但之后不能再访问该句柄。Blob 完成事件返回的是 Flash 对象句柄：长度
最多 768 KiB，可读取和再次发送，但不可用 `buffer_write_u8` 或 `buffer_append_text` 修改。
设备当前只有一个 Blob Flash 对象槽；接受下一文件会使旧对象句柄失效。

句柄只在本次前台 owner 生命周期有效。不得写入 KV、猜测位结构、跨插件传递或在重新打开
后复用。无论插件是否主动释放，宿主退出路径都会全部回收。

### 8.6 系统 Nearby API

Nearby 是系统服务，不是插件自建 BLE 服务。插件申请租约后，固件以 BLE Peripheral/GATT
Server 广播固定 Runtime Gateway；客户端连接后必须先提交当前设备码。设备码校验、连接
代次、GATT UUID、分片、Flash 写入、SHA-256、ADPCM 和音频锁都由系统实现，插件只处理
高层事件。插件不能注册任意 GATT characteristic，也不能绕过设备码直接访问 NimBLE。

基本调用：

```json
["nearby_acquire"]
["nearby_send", 0, 1]
["nearby_blob_send", 0, "note.txt", "text/plain", 2]
["nearby_release"]
```

| 指令 | 需要权限 | 行为 |
| --- | --- | --- |
| `nearby_acquire` | `nearby` | 取得当前前台插件的唯一通信租约并启动 Runtime Gateway；重复调用幂等 |
| `nearby_release` | `nearby` | 停止语音、取消传输并关闭插件的 BLE Gateway；RAM/对象句柄仍可在前台内使用 |
| `nearby_send` | `nearby` | 发送 RAM 缓冲区（0–4096 字节），把系统 message ID 写入目标槽 |
| `nearby_blob_accept` | `nearby` | 接受 `BLOB_OFFER` 的 transfer ID，异步擦除文件区后通知对端开始发送 |
| `nearby_blob_reject` | `nearby` | 拒绝待处理的 transfer ID |
| `nearby_blob_send` | `nearby` | 发送 RAM 缓冲区或 Blob 对象；名称最多 63、MIME 最多 47 UTF-8 字节 |
| `nearby_voice_start` | `nearby`、`audio`、`microphone` | 开始半双工语音会话，初始为接收/播放 |
| `nearby_voice_transmit` | 同上 | 参数是状态槽；非 0 开始采集发送，0 回到接收播放 |
| `nearby_voice_stop` | 同上 | 停止采集/播放并释放独占音频会话 |

`nearby_acquire` 成功只表示系统已开始广播，不表示客户端已经连接。客户端必须订阅 TX、连接、
提交完整 10 字符设备码，插件收到 `STATE value=2` 后才可发送；过早发送会得到 Host error。
所有发送都是异步的，ID 只用于关联后续事件，不表示发送已经完成。

`nearby` handler 的 `type` 合同：

| type | 名称 | `id` | `handle` | `value` |
| ---: | --- | --- | --- | --- |
| 1 | `STATE` | 0 | 0 | 0 断开、1 已连接、2 设备码通过、3 设备码错误 |
| 2 | `MESSAGE` | message ID | RAM 数据句柄 | 字节数 |
| 3 | `MESSAGE_SENT` | message ID | 0 | 字节数 |
| 4 | `BLOB_OFFER` | transfer ID | `name + "\n" + MIME` 元数据句柄 | 文件总字节数 |
| 5 | `BLOB_PROGRESS` | transfer ID | 0 | 已落盘字节数 |
| 6 | `BLOB_READY` | transfer ID | 只读 Flash 对象句柄 | 文件总字节数 |
| 7 | `BLOB_SENT` | transfer ID | 0 | 0；仅在对端返回匹配摘要的 ACK 后产生 |
| 8 | `BLOB_REJECTED` | transfer ID | 0 | 0 |
| 9 | `VOICE_STATE` | 0 | 0 | 0 停止、1 接收、2 采集发送 |
| 10 | `ERROR` | 相关 ID 或 0 | 0 | 下表错误码 |

`ERROR value`：1 协议错误、2 超过容量、3 无空闲缓冲区、4 分片乱序、5 Flash 错误、
6 SHA-256 不匹配、7 传输不可用、8 资源忙、9 音频错误。错误不会用静默重试掩盖；插件应
根据 ID 更新 UI，并等待重新连接或让用户重试。

小消息必须完整重组后才派发，最大 4096 字节。Blob 最大为 `nearby_data` 分区的 768 KiB，
接收前必须由插件明确 accept；数据按严格 offset 写入，完成后核对 offer 中的 SHA-256，只有
摘要一致才产生 `BLOB_READY`。当前同一时间只允许一个 Blob 流，文件区是临时对象存储，不是
插件永久文件系统；要持久化少量整数仍使用隔离 KV。

设备发送 Blob 时，`nearby_blob_send` 返回 transfer ID 后只表示任务已排队。系统保留源句柄，
等待对端 decision；对端接受后才发送数据与 complete，且只有收到大小和 SHA-256 都匹配的
ACK 才释放源句柄并派发 `BLOB_SENT`。拒绝、断线、错误 ACK 或插件退出都会取消该流并回收资源。

语音固定为 16 kHz、16-bit、mono、20 ms（320 sample）一帧，使用独立块 IMA-ADPCM，编码后
164 字节，可放入一次 MTU 256 的 GATT 帧。它是按键对讲式半双工，不是 BLE Audio、A2DP
或电话级全双工；发送时丢弃来向语音，接收时使用 6 帧有界队列。系统持有 ES8311 音频会话，
因此同一时刻不会与 `tone` 抢改采样格式。

参考实现见
[`examples/plugins/nearby-demo/plugin.json`](../examples/plugins/nearby-demo/plugin.json)，
客户端协议与 UUID 见 [`PLUGIN_SYSTEM.md`](PLUGIN_SYSTEM.md)。打开 Nearby Demo 并看到 Runtime
广播后，可用参考客户端验证：

```bash
python3 tools/nearby_client.py --device-code XXXXX-XXXXX message "你好"
python3 tools/nearby_client.py --device-code XXXXX-XXXXX send-file ./note.txt
python3 tools/nearby_client.py --device-code XXXXX-XXXXX \
  --listen 0 --output-directory ./received listen
```

### 8.7 退出

```json
["exit"]
["end"]
```

`exit` 不需要权限，用于在 OK 或 back 等用户事件结束后请求产品壳返回主页。没有 back
handler 的插件无需显式使用它。插件可在 back 中主动 release 以尽早关闭硬件，但即使省略，
产品壳真正关闭插件时也会执行同一系统清理；VM fault 和 start 失败同样如此。

## 9. 权限规范

| 权限 | 允许的能力 | 设备批准页显示 | 缺少权限时 |
| --- | --- | --- | --- |
| `storage` | `kv_load`、`kv_save` | 存储 | 打包器拒绝；VM 也会拒绝 |
| `audio` | `tone` | 音频 | 打包器拒绝；无音频硬件时条目不可进入 |
| `nearby` | acquire/release、消息与 Blob | 近场通信 | 打包器拒绝；Nearby 服务不可用时条目不可进入 |
| `settings` | `setting_load/save`、`theme_next`、`device_info` | 系统设置 | 打包器拒绝；设置服务不可用时条目不可进入 |
| `microphone` | 与 `nearby+audio` 共同启用语音采集 | 麦克风 | 打包器拒绝；无音频硬件时条目不可进入 |

坚持最小权限。`nearby` 只授予固定 Runtime Gateway，不授予任意 BLE、Wi-Fi、Socket 或
ESP-IDF 网络栈。`microphone` 单独声明没有录音入口；三个语音指令会同时检查 `nearby`、
`audio`、`microphone`，避免把播放权限等同于采集权限。

打包器根据实际指令强制权限，设备 VM 运行时再次检查，因此删除权限字段来绕过批准信息不会
得到对应能力。

## 10. 主题插件规范

主题插件是签名 `.fpp` 中的一种纯数据包，不执行 VM 字节码，也没有权限。它只能修改宿主
已经公开的设计 Token，不能注入 LVGL 对象、替换字体、执行 handler 或访问其他插件数据。
这是有意的边界：主题切换不会获得额外系统能力，也不会让普通插件的逻辑发生变化。

完整源码结构：

```json
{
  "kind": "theme",
  "id": "theme.example.midnight",
  "name": "午夜蓝",
  "author": "Example",
  "version": 1,
  "host_api": 5,
  "theme": {
    "colors": {
      "background": "#0B1726",
      "surface": "#14283D",
      "text": "#F2F7FA",
      "text_muted": "#A9BBC8",
      "accent": "#163E5C",
      "accent_strong": "#0E2B42",
      "selection": "#245F85",
      "muted_surface": "#21384A",
      "danger": "#7D2632",
      "success": "#1E664A",
      "border": "#75CFFF",
      "selection_border": "#FFFFFF"
    },
    "panel": {
      "radius": 8,
      "border_width": 2,
      "shadow_width": 3,
      "shadow_x": 2,
      "shadow_y": 3
    },
    "decoration": "none"
  }
}
```

强制合同：

- ID 必须以 `theme.` 开头；app 反过来不能占用这个命名空间。
- `colors` 必须精确定义 12 个 Token，不能缺少或增加字段，值必须是具体 `#RRGGBB`。
- `radius` 为 `0..16`，`border_width` 为 `0..6`，`shadow_width` 为 `0..12`，
  `shadow_x/shadow_y` 为 `-12..12`。
- `decoration` 只能为 `none` 或 `pixel_ground`。
- 主题包不能包含 `permissions`、`state_slots`、`templates`、`handlers`；固定 payload 为
  `THM1` schema v1、64 字节。
- 主题名称和作者仍必须属于公共 14 px 字库。主题不能携带字体、位图或额外资源。

安装流程和普通 app 相同：BLE 传输、验签、设备批准、原子提交。主题会显示在“插件”页的
已安装列表中并占用一个插件槽，但不会出现在主页 app 列表。安装后到“设置 → 主题”切换；
活动主题 ID 保存在 NVS。卸载活动主题时系统立即回退到内置“像素原野”，不会留下悬空索引。

主题自动作用于固件自带主页、设置、插件管理、设备页，以及所有语义组件、宿主动作栏和
宿主弹窗。Canvas 中写死的颜色保持插件设计；使用 `theme:token` 颜色引用的 Canvas 对象在
下一次重绘时跟随主题。参考包见
[`examples/plugins/midnight-theme/plugin.json`](../examples/plugins/midnight-theme/plugin.json)。

## 11. `system.settings` 特殊规范

“万物皆插件”并不表示所有系统 ID 都可覆盖。下载版设置是当前唯一受支持的系统覆盖包，必须：

1. `id` 精确为 `system.settings`；
2. `host_api` 精确为 5；
3. 声明 `settings` 权限；
4. 实际包含 `device_info` 指令，保留设备信息入口；
5. 实际包含 `theme_next` 指令，保留主题切换入口；
6. 在顶层 back 执行 `exit`，在设备信息子页 back 先回设置列表；
7. 保持亮度、音量、按键音、自动息屏和主题的取值合同。

安装后，主页顶部“设置”的位置、名称和图标仍由 Registry 固定，进入时运行下载包。包启动
失败、被卸载或不满足覆盖策略时自动使用内置设置。下载版设置包会出现在“插件”页的已安装
包列表中，可以卸载并回到内置实现。

`system.plugins` 是安装、设备码和恢复入口，不能由下载包替换。其他 `system.*` ID 也由
固件保留；打包器会拒绝。

## 12. 包、签名和当前格式

### 12.1 当前版本矩阵

| 层 | 当前版本 | 接受规则 |
| --- | ---: | --- |
| `.fpp` package | 1 | 只接受 v1 |
| Manifest | 5 | 只接受 magic `PLG5` 和 v5 |
| Bytecode | 1 | 只接受 v1 |
| Host API | 5 | 只接受 v5 |

V2.6.0 不含旧插件格式的解析或迁移代码。`PLG4`、Host API 4 和更早包即使还留在 Flash 中
也不会进入 Registry；安装新包时这些无效槽可直接被覆盖。所有插件必须用当前仓库的
`plugin_tool.py` 重新打包并重新安装。

### 12.2 包大小与资源上限

- `.fpp` 总大小最多 `0x3F000` 字节，即 252 KiB，包含 108 字节包头。
- 插件没有私有堆，也没有声明堆大小的 Manifest 字段。
- 最多同时安装 7 个活动 ID；Flash 有 8 个槽，始终留一个槽做原子更新。
- 每槽 256 KiB，其中前 4 KiB 是提交头，其余最多 252 KiB 存包。
- 更新流程先写空槽、重新验签、最后提交魔数，再擦旧槽；断电不会先破坏旧包。

### 12.3 二进制布局（工具作者附录）

所有整数均为 little-endian，签名的 `r`、`s` 各自使用 32 字节 big-endian：

```text
Package header，108 bytes
  0x00  char[4]  "FPP1"
  0x04  u16      package version = 1
  0x06  u16      header size = 108
  0x08  u32      signed content size
  0x0C  u8[32]   SHA-256(prefix[0..11] + content)
  0x2C  u8[64]   ECDSA P-256 raw r || s
  0x6C            signed content

Signed content
  0x000  Manifest，固定 192 bytes
  0x0C0  app: bytecode，code_size bytes + NUL 分隔 UTF-8 string table
         theme: 固定 64-byte THM1 payload，strings_size = 0
```

Manifest 的稳定偏移：

| 偏移 | 字段 |
| ---: | --- |
| 0 | `"PLG5"` |
| 4/6 | Manifest version / bytecode version，均 u16 |
| 8 | Host API version，u16，必须为 5 |
| 10/11 | kind / payload schema，均 u8；kind 为 0 app、1 theme |
| 12/16 | plugin version / permissions，均 u32 |
| 20 | 4 字节保留区，必须全为 0 |
| 24 | state slots，u16；26–27 保留且必须为 0 |
| 28/32 | code size / strings size，u32 |
| 36 | 11 个连续事件入口：start、up、down、ok、timer0..3、back、action、nearby |
| 80 | ID，32 字节 NUL 结尾 ASCII |
| 112 | name，48 字节 NUL 结尾 UTF-8 |
| 160 | author，32 字节 NUL 结尾 UTF-8，结束于 192 |

无 handler 的入口值为 `0xFFFFFFFF`。开发插件时不要手工拼二进制；这些偏移用于实现其他
语言的当前格式打包器和排查损坏包。

permissions 位：bit 0 `storage`、bit 1 `audio`、bit 2 `nearby`、bit 3 `settings`、
bit 4 `microphone`；其余位必须为 0。Manifest v5 不保留尾部 4 字节，新增的第 11 个 handler
正好占用旧偏移 76–79，因此 ID 从 80 开始。

## 13. 测试规范

一个可发布插件至少完成四层检查：

### 13.1 打包器检查

```bash
python3 tools/plugin_tool.py pack SOURCE \
  --private .keys/plugin-signing-private.pem \
  --output OUTPUT.fpp
python3 tools/plugin_tool.py inspect OUTPUT.fpp \
  --public .keys/plugin-signing-public.pem
```

### 13.2 仓库主机测试

```bash
tests/run_host_tests.sh
```

该脚本检查公共字库、包格式、VM、设备码、插件管理状态模型、Nearby 帧/ADPCM、Counter、
Settings、Meteor Tap、Nearby Demo、主题 payload、Python 工具与签名包解析。新增复杂插件时，应仿照
`tests/test_settings_plugin.c` 或 `tests/test_meteor_tap_plugin.c`，用假的 Host callback 验证
每个事件后的状态、UI 调用、KV、定时器和退出请求。

### 13.3 固件构建

```bash
idf.py build
idf.py size
```

插件 JSON 本身不进入固件，但公共字体、VM 或 Host 修改必须做完整 ESP-IDF 构建。不要把
主机测试通过等同于设备运行通过。

### 13.4 实机用例

至少验证：

1. 首次安装、同 ID 同版本重装、同 ID 升级和降级拒绝；
2. 主页名称、中文和图标无方块；
3. 快速连续 UP/DOWN 每次只产生预期的一次动作；
4. OK 短按与长按 back 不串事件；
5. 每个子页逐层返回，顶层最终回主页；
6. 重启后 KV 恢复，普通运行状态按设计重置；
7. 定时器在退出后停止，反复进入退出无持续堆下降；
8. 音效开关、全局音量和无音频硬件降级正确；
9. 插件页能卸载该包，主页立即刷新；
10. 传输、验签、等待批准和更新阶段断电后仍有完整旧包或新包可用。
11. 安装主题后可切换，主页、自带页面、语义插件和宿主动作栏/弹窗一致换色；
12. 卸载当前主题后回退内置主题，重启后无悬空选择。
13. Nearby 客户端每次重连都必须重新提交设备码，错误码不能收发数据；
14. 消息、Blob 接受/拒绝、SHA 错误和语音收发事件的 ID/handle/value 正确；
15. 在消息、Blob、语音进行中直接长按退出，BLE、音频、句柄和工作队列均被回收；
16. 让 handler 触发 VM fault 后重复打开其他插件，不出现旧事件或旧音频。

## 14. 常见错误与根因定位

| 现象/错误 | 常见根因 | 正确检查 |
| --- | --- | --- |
| `outside the public 14px plugin font` | 使用 emoji、扩展汉字或未收录私用区图标 | 看错误中的 U+码点，换 GB2312 字符或正式扩展公共字库 |
| `requires the ... permission` | 指令和权限数组不一致 | 添加最小所需权限，不要删除指令检查 |
| `host_api must be ...` | 缺少 `host_api`、值不是 5，或包不是当前格式 | 设置 `"host_api": 5` 并用当前工具重新打包 |
| `signature does not match` | 私钥与固件公钥不匹配，或包被改动 | 用对应公钥 inspect；必要时重刷信任该公钥的固件 |
| 设备验签失败 | 包不完整、摘要/签名错误、Manifest/字符表非法 | 先本地 inspect，再看设备日志中的 ESP 错误 |
| 打开后“host error” | 非法设置值、UI 对象超限、音效队列满或 Host 参数越界 | 按事件缩小 handler，检查本章各资源边界 |
| `stack underflow/overflow` | 分支间栈不平衡或嵌套过深 | 为每条控制流手工标注栈效果，保证进入 `end` 前合理 |
| `budget exceeded` | 无限循环或展开后的渲染路径过长 | 去掉 VM 内循环，改用 timer 分帧，减少重复绘制 |
| 页面出现色块遮字 | 后创建的大矩形覆盖了先创建的文字 | 每帧 clear，背景先画，文字最后画 |
| 主题切换后 Canvas 颜色不变 | 使用了固定 `#RRGGBB`，或切换后没有重绘 | 需要跟随的颜色改成 `theme:token`，主题变化后完整重绘 |
| 弹窗确认后没有动作 | 缺少 `action` handler，或没有读取正确 dialog ID/value | 实现 action，用 event_load 读取两个字段后按 ID 分派 |
| Nearby acquire 后不能发送 | 客户端尚未订阅 TX、未连接或设备码未通过 | 等 `nearby` 的 `STATE value=2`，检查客户端同步顺序 |
| 收几条消息后 no buffer | `MESSAGE`/元数据句柄未 release | 每次处理完 `handle` 后调用 `buffer_release`；退出时系统只做最终兜底 |
| Blob 收到后摘要失败 | offset 乱序、对端 total/digest 不一致或传输中断 | 从 offer 的原始字节计算 SHA-256，严格连续发送，失败后重新 offer |
| 退出后仍收到旧事件 | 生命周期实现错误，不应由插件兼容 | 检查 owner generation、宿主统一清理和工作项代次，禁止按 ID 猜测吞事件 |
| 重装后旧分数仍在 | 卸载按规范保留隔离 KV | 使用新 ID，或在插件逻辑中提供重置并写入默认值 |
| 已发送但主页没有插件 | 只完成传输/验签，未在设备批准；或包使用系统覆盖 ID | 看设备状态，短按 OK 批准；检查 ID 与 Registry 策略 |

设备显示“插件错误”时，应先看 USB 日志中的 `plugin_host` / `plugin_vm` 结果名称，定位到
失败事件，再对照指令预算、权限和宿主参数。不要用增加延时、吞掉错误或重复重试来掩盖
确定性的 VM/Host 合同错误。

## 15. 发布检查清单

- [ ] ID 使用自有命名空间，发布后保持不变。
- [ ] 版本号不低于已发布包。
- [ ] 只声明实际使用的权限；语音同时声明 `nearby`、`audio`、`microphone`。
- [ ] `host_api` 显式且精确为 5。
- [ ] `start` 能从全零状态和 KV 缺失状态启动。
- [ ] 所有分支栈平衡，最坏路径不超过 512 条指令。
- [ ] 状态槽不超过 16，索引均在声明范围内。
- [ ] 语义帧先 `ui_screen`，Canvas 帧先 `ui_clear`；最终都 `ui_commit`。
- [ ] Canvas UI 对象请求不超过 24，背景先于文字；需要换肤的颜色使用 `theme:token`。
- [ ] 底部按键语义使用 `ui_action_bar`，不在主体中复制一套提示。
- [ ] 确认动作使用宿主弹窗，并在 `action` handler 中按 dialog ID 处理异步结果。
- [ ] 所有插件文字使用公共 14 px 字库。
- [ ] KV 键不超过 8 个，单事件操作不超过 8 次，不高频写 Flash。
- [ ] 定时器最短 100 ms，退出后不依赖其继续工作。
- [ ] 每个收到的消息/Blob 元数据句柄都有正常 release 路径；异常退出交给系统强制回收。
- [ ] 发送前等待 `STATE value=2`，异步完成按 message/transfer ID 处理。
- [ ] 不假设后台运行；插件退出后不依赖 BLE、语音、timer 或回调继续工作。
- [ ] 有子页时实现 back；顶层 back 执行 exit。
- [ ] `.fpp` 小于等于 252 KiB，并用匹配公钥验证签名。
- [ ] 主机测试、固件构建和实机用例分别记录结果。

## 16. 自带插件采用的规范与参考实现

固件内置实现和仓库随附包必须遵守同一交互规范，不允许再维护另一套按钮提示、弹窗或主题
常量。当前迁移结果：

| 功能 | 模式 | 复用的宿主能力 |
| --- | --- | --- |
| 主页 | 固件语义组件 | 状态栏/电量、主题面板、空状态、统一动作栏 |
| 内置设置 | 固件语义组件 | 统一行、值格式、主题切换、设备页、动作栏 |
| 插件管理 | 固件语义组件 | 统一行、安装/卸载弹窗、动作栏、主题列表 |
| Counter 下载包 | 语义 UI | 数值卡片、动作栏、异步确认弹窗、action |
| Settings 下载包 | 语义 UI | 6 行设置、自动窗口、主题值、设备页、逐层 back |
| Meteor Tap 下载包 | 混合模式 | 自绘游戏主体；宿主状态栏和动作栏 |
| Nearby Demo 下载包 | 语义 UI | 前台租约、设备码状态、消息与 Blob 事件、缓冲句柄 |
| Midnight Theme 下载包 | theme 数据包 | 12 个颜色 Token、面板样式、无可执行代码 |

- [`examples/plugins/counter/plugin.json`](../examples/plugins/counter/plugin.json)：状态、KV、
  音效、语义数值卡片和异步确认弹窗的最小完整示例。
- [`examples/plugins/meteor-tap/plugin.json`](../examples/plugins/meteor-tap/plugin.json)：自由绘制
  游戏主体与宿主动作栏共存的混合模式、模板、定时器和复杂控制流。
- [`examples/plugins/settings/plugin.json`](../examples/plugins/settings/plugin.json)：Host API 5、
  语义列表、全局设置、主题切换、设备信息子页和逐层 back。
- [`examples/plugins/midnight-theme/plugin.json`](../examples/plugins/midnight-theme/plugin.json)：
  最小完整主题包。
- [`examples/plugins/nearby-demo/plugin.json`](../examples/plugins/nearby-demo/plugin.json)：
  Nearby acquire/release、消息发送、Blob 接受和事件字段的完整最小示例。
- [`tools/plugin_tool.py`](../tools/plugin_tool.py)：JSON 规范、编译、签名和检查的可执行事实来源。
- [`components/plugin_runtime/src/plugin_vm.c`](../components/plugin_runtime/src/plugin_vm.c)：
  字节码运行语义与错误行为的可执行事实来源。
- [`main/plugin_host.c`](../main/plugin_host.c)：UI、KV、音效、定时器、前台 owner 和统一回收边界。
- [`main/nearby_service.c`](../main/nearby_service.c)：设备码之后的消息、Blob、语音与句柄生命周期。
