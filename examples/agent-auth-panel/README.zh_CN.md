<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# Agent 授权面板

这是一个可安装的 Lua App，用于显示外部 Agent 发来的授权请求。用户可以在
最多三个选项之间移动，并通过 Passport Link 把选择结果发回 Agent。

Agent 发送请求前，授权面板必须已经处于前台运行状态。本插件不增加身份认证，
现有 Passport Link 的设备码目标检查是唯一的寻址检查。

打包并检查：

~~~bash
python3 tools/pack_pap.py examples/agent-auth-panel examples/agent-auth-panel/dist/agent-auth-panel.pap
python3 tools/inspect_pap.py examples/agent-auth-panel/dist/agent-auth-panel.pap
~~~

应用层 payload 使用紧凑 JSON：

~~~json
{"v":1,"kind":"request","rid":"a-001","title":"执行命令","message":"是否执行 npm test","options":[["once","本次执行"],["cancel","取消"]]}
~~~

Link payload 必须保持在 200 个 UTF-8 字节以内。文本字段不能包含控制字符；双引号
和反斜杠使用标准 JSON 转义，并由所有 PAP 共用的 `passport.json` 系统 API 解码。
source code、request ID 与规范化内容均相同的重复请求会直接重发上次结果，不会再次
询问用户；这只是传输重试机制，不是安全功能。
