<p align="right">
  <strong>简体中文</strong> · <a href="agent-authorization.md">English</a>
</p>

# Agent 授权面板

Agent 授权面板是一个以 PAP 发布的前台 Lua 应用。Agent 侧插件或桥接程序通过 Passport Link 发送紧凑 JSON 请求；面板展示请求和选项，用户确认一个选项后，面板再通过同一条 Link 连接发送紧凑 JSON 响应。

当前实现刻意保持集成面较小：

- 面板必须提前打开，并且保持在前台。
- 传输使用现有的 Passport Link 消息帧。
- 应用层载荷使用 UTF-8 编码的紧凑 JSON。
- 请求不强制要求 deny 或 cancel 选项。
- 寻址只检查设备码是否匹配。这不是认证或授权安全边界。

## PAP 示例

示例源码位于 examples/agent-auth-panel：

~~~text
examples/agent-auth-panel/
├── manifest.json
├── main.lua
└── README.md
~~~

构建命令：

~~~bash
python3 tools/pack_pap.py examples/agent-auth-panel examples/agent-auth-panel/dist/agent-auth-panel.pap
~~~

当前示例 manifest 使用 com.folotoy.agent-auth。这个 ID 不是协议强制要求；Passport Link 会根据 manifest ID 计算服务命名空间，因此如果修改了 ID，Agent 侧也必须使用相同的 ID。

## 应用层载荷

所有载荷都包含 v=1。请求使用 kind=request：

~~~json
{"v":1,"kind":"request","rid":"demo-001","title":"执行命令","message":"是否执行 npm test","options":[["once","执行一次"],["always","总是允许"],["cancel","取消"]]}
~~~

面板实现的字段规则：

- rid：1 到 24 字节，用于标识一个来源发起的请求。
- title：1 到 24 字节。
- message：1 到 72 字节。
- options：1 到 3 个选项，每个选项由选项 ID 和显示标签组成。
- 选项 ID：1 到 16 字节。
- 选项标签：1 到 18 字节。
- 文本使用 UTF-8 JSON 字符串，不包含控制字符、双引号或反斜杠。
- 完整载荷最多 200 字节，与当前 Link 载荷上限一致。

面板默认选中第一个选项。按 UP、DOWN 在选项间移动，双击时跨过两个位置。按 OK 发送响应：

~~~json
{"v":1,"kind":"response","rid":"demo-001","status":"selected","option":"once"}
~~~

当需要报告传输层状态时，面板还会使用以下状态值：

~~~text
busy       面板正在展示另一个请求。
conflict   相同请求 ID 收到了不同内容。
cancelled  面板离开当前请求，但没有选择选项。
~~~

只有 selected 响应包含 option。cancel 消息可以请求面板离开匹配的请求：

~~~json
{"v":1,"kind":"cancel","rid":"demo-001"}
~~~

未知或格式错误的应用层载荷会被忽略。若收到相同来源设备码、相同请求 ID 且内容完全相同的重复请求，面板会重发缓存的响应，不再显示第二次提示，从而允许发送端在响应丢失时重试。相同来源和请求 ID 如果对应的内容发生变化，则返回 conflict。

## Passport Link 寻址

Link 帧包含来源设备 ID、目标设备 ID、服务 ID、序号和应用层载荷。服务 ID 是 manifest ID 的 UTF-8 字节序列经过 FNV-1a 计算得到的哈希值。当前示例 manifest 的哈希值为 0x7e22d01e。

Web Demo 使用的 Web Bluetooth 特征 UUID：

~~~text
Link RX  01000000-0058-5254-524f-505353415031
Link TX  01000000-0058-5454-524f-505353415031
~~~

面板只接收目标服务命名空间与自身匹配的消息。Web Demo 将面板设备码作为目标地址，将可编辑的来源设备码作为来源地址，并监听现有 Link TX 特征上的响应通知。

当前实现没有增加配对、签名、加密、独立的重放保护或更强的对端认证。设备码是地址，不是秘密。

## Web Demo

通过本地 HTTP 服务器打开 web/agent-auth-demo.html。它通过 Web Bluetooth 连接已经打开的面板，并提供以下可编辑字段：

- 目标设备码；
- 来源设备码；
- manifest/service ID；
- 请求 ID、标题、消息和最多三个选项。

页面会在发送前显示最终 JSON 载荷和字节数，也支持发送 cancel 消息、接收并解析面板响应，以及查看事件日志。页面使用与固件一致的 UUID 和小端序 Link 帧布局。

由于 Web Bluetooth 需要浏览器权限流程，页面应从 localhost 或其他允许的安全来源提供。Passport 设备需要在附近、已连接，并且已经订阅 Link 特征。
