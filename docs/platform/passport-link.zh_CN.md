<p align="right"><a href="passport-link.md">English</a> · <strong>简体中文</strong></p>

# Passport Link v1 与 BLE 安装协议

## 设备码

设备码由 ESP32-C3 工厂 MAC 的 48 位唯一值编码成十个 Base32 字符，再增加一位输入校验字符，例如 `22222-22222-2`。安装页将这个值呈现为配对码。它公开可分享，只用于设备发现、寻址和避免误发，不承担保密或身份认证职责。

完整 BLE 名称通过 scan response 返回：

```text
Passport-22222-22222-2
```

主广播包含 128 bit Passport Service UUID，Web Bluetooth 才能通过 service filter 发现设备。连接后客户端仍必须读取 Device Code characteristic，并把读到的值用于目标复核。

## App 消息帧

固定 36 B 头：

| 字段 | 大小 | 说明 |
| --- | ---: | --- |
| magic | 2 | `PL` |
| version | 1 | `1` |
| type | 1 | 消息/文件/流 |
| source_id | 8 | 公开源设备 ID |
| target_id | 8 | 必须匹配本机才能投递 |
| service | 4 | App manifest ID 的 FNV-1a |
| sequence | 4 | 发送序号 |
| payload_len | 2 | V1 最大 200 B |
| reserved | 2 | 0 |
| payload_crc32 | 4 | IEEE CRC-32 |

系统先校验版本、长度、CRC 和 target，再把当前 App namespace 的帧交给插件。

Web Bluetooth 客户端把完整帧写入 Link RX，并订阅 Link TX 接收 PAP 响应：

```text
Link RX  01000000-0058-5254-524f-505353415031
Link TX  01000000-0058-5454-524f-505353415031
```

service 字段是 App Manifest ID 的 UTF-8 字节经过 FNV-1a 计算得到的哈希值。只有前台 App 会接收其命名空间下的帧。

## BLE 安装

GATT UUID：

```text
Service      0100004b-4e49-4c54-524f-505353415031
Device Code  01000045-444f-4354-524f-505353415031
Package Ctrl 01000043-474b-5054-524f-505353415031
Package Data 01000044-474b-5054-524f-505353415031
Package Stat 01000053-474b-5054-524f-505353415031
```

开始控制包为 little-endian：`op:u8=1 + total_size:u32 + crc32:u32 + target_id:u64`。系统比较 `target_id` 后才打开 staging 文件。客户端应先订阅 Package Stat，以有响应写入发送 begin，并等待 `开始接收`；随后用每片 180 B 的有响应写入发送 Package Data，最后向 Package Ctrl 写单字节 `0x02`，等待 `安装成功` 或失败状态。数据写入的响应是固定队列所需的背压，客户端不能把“传输完成”当作“安装成功”。

NimBLE 回调只复制小分片到固定队列；文件写入、CRC 和 `.pap` 安装在 `pap_install` 工作任务执行。

无外部依赖的[共享网页工具](../../web/installer.html)实现了完整流程。通过 HTTPS 或 localhost 提供 `web/`，在桌面版 Chrome 或 Edge 中输入配对码并连接一次。浏览器设备选择器会发现广播 Passport Service UUID 的设备；连接后网页读取 Device Code characteristic，并要求完整设备码一致。之后，同一条已验证 GATT 连接既可安装本地 `.pap`，也可执行 [2FA 验证器 PAP](totp-authenticator.zh_CN.md)记录的密钥发送流程。Web Bluetooth 首次允许站点访问设备时，浏览器会强制要求一次明确的授权确认；网页可以直接重连已授权且配对码相符的设备，不再打开设备列表。命令行安装仍可使用 `tools/ble_install.py`。

## 安全边界

- 不使用 BLE 系统配对/绑定。
- 不隐藏设备码，不把设备码视为密码。
- V1 不解决窃听或恶意写入；产品目标仅是减少发送到错误设备的概率。
- 通过 Link 发送的应用密钥（包括 TOTP seed）处于明文边界内。
- Package Service 仍执行路径穿越拦截、尺寸限制和 CRC，避免损坏文件破坏目录。
