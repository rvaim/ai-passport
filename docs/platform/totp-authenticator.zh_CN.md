<p align="right">
  <strong>简体中文</strong> · <a href="totp-authenticator.md">English</a>
</p>

# 2FA 验证器 PAP

“2FA 验证器”是位于 `examples/totp-authenticator` 的可安装 Lua PAP。它通过 Passport Link 接收 GitHub Pages 共享工具发送的 TOTP 账号，把账号写入 App 私有持久化存储，并在设备上生成 RFC 6238 验证码。

职责边界刻意保持清晰：

- 固件只提供通用易失性 `passport.clock` 与路由级 `passport.app.on_tick` API。
- PAP 实现 Base32 解码、HMAC-SHA1、移动计数器、动态截断、账号校验、持久化和 2FA 界面。
- 浏览器通过现有明文 Passport Link 连接发送当前 Unix 时间与账号记录。

## 安装与发送密钥

1. 运行 `python3 tools/pack_pap.py examples/totp-authenticator examples/totp-authenticator/dist/totp-authenticator.pap` 打包。
2. 打开 GitHub Pages 工具，在页面顶部输入设备码并连接一次。
3. 使用“Passport 安装器”安装本地 `.pap`。
4. 在设备上打开“2FA 验证器 → 接收 2FA 密钥”。
5. 在“2FA 密钥发送”中粘贴 `otpauth://totp` URI，或填写签发方、账号、Base32 密钥、验证码位数与周期，然后发送。

每次发送账号时，网页都会附带浏览器当前 Unix 时间；账号已经存在时，也可以单独点击“同步时间”。添加账号期间必须保持设备接收页面打开；只同步时间的消息可在 PAP 位于前台时接收。

## 应用层载荷

所有载荷都是紧凑 UTF-8 JSON，并受 Passport Link 200 字节上限约束。添加账号请求为：

~~~json
{"v":1,"k":"add","q":"a01","i":"Example","a":"alice@example.com","s":"JBSWY3DPEHPK3PXP","d":6,"p":30,"t":"1730000000"}
~~~

字段规则：

- `v`：固定为 `1`。
- `k`：添加账号时为 `add`，只同步时间时为 `time`。
- `q`：三个小写 Base36 字符；PAP 会原样回传这个请求标记，避免延迟响应误确认后续发送。
- `i`：签发方，0～24 个 UTF-8 字节。
- `a`：账号标签，1～48 个 UTF-8 字节；签发方与账号合计最多 52 字节。
- `s`：规范化且无填充的 Base32 密钥，长度 16～64，只能使用 `A-Z2-7`。
- `d`：6 位或 8 位。
- `p`：15～120 秒的整数周期。
- `t`：UTC 2024-01-01 至 9999-12-31 之间的十进制 Unix 秒字符串。

只同步时间的请求为：

~~~json
{"v":1,"k":"time","q":"a02","t":"1730000000"}
~~~

只有原子存储写入成功后，PAP 才返回 `{"v":1,"k":"added","q":"a01"}`；时间同步有效时返回 `{"v":1,"k":"time","q":"a02"}`。失败响应为 `{"v":1,"k":"error","q":"a01","e":"code"}`。未知字段、错误 Base32、不支持的算法、越界值和超限载荷都会被拒绝。

再次发送相同签发方/账号组合会替换原记录；达到 12 个账号后会拒绝新的组合。PAP 不记录或回传已保存密钥。如果持久化状态损坏或无法读取，它会报告失败，不会静默覆盖原文件。

## 时间与验证码计算

开发板没有电池供电的墙上时钟。`passport.clock.sync` 会把传入的 Unix 时间锚定到 ESP32-C3 单调计时器，因此固件持续供电时可跨 PAP 启停保留。重启或断电会使时间失效，必须再次点击“同步时间”后才能信任验证码。

TOTP 计算仍完整位于 Lua PAP。插件会解码 Base32 密钥，在不依赖 Lua 64 位数值的情况下构造大端 64 位时间步计数器，计算 HMAC-SHA1，执行 RFC 4226 动态截断，并显示配置的 6 位或 8 位验证码及一秒刷新倒计时。

## 传输与安全边界

Manifest ID 为 `com.folotoy.totp-authenticator`，Passport Link 根据该 ID 计算服务命名空间。浏览器把 Link 帧写入现有 Link RX 特征，并在 Link TX 上等待响应；发送前会复核公开设备码，以减少误发到其他 Passport 的风险。

Passport Link v1 按需求不使用 BLE 配对、应用层加密或对端认证。因此 2FA 密钥会以明文经过 BLE，附近的监听者或写入者不在本功能保护范围内。PAP 私有存储与其他 App 隔离，但它属于 FAT 存储，并非硬件安全存储。

主机测试位于 `tests/test_totp_authenticator_plugin.lua` 与 `tests/test_passport_totp_protocol.mjs`。真机验收仍需覆盖安装、密钥发送、PAP 重启后的持久化、设备重启后的时间重同步，以及与已知 RFC 6238 实现的验证码对照。
