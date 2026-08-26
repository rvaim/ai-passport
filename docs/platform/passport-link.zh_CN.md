<p align="right"><a href="passport-link.md">English</a> · <strong>简体中文</strong></p>

# Passport Link v1 与 BLE 安装协议

## 设备码

设备码由 ESP32-C3 工厂 MAC 的 48 位唯一值编码成十个 Base32 字符，再增加一位输入校验字符，例如 `ABCDE-FGHIJ-K`。它公开可分享，只用于设备发现、寻址和避免误发，不承担保密或身份认证职责。

BLE 广播名为：

```text
Passport-ABCDE-FGHIJ-K
```

连接后客户端还应读取 Device Code characteristic 复核一次。

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

## BLE 安装

GATT UUID：

```text
Service      0100004b-4e49-4c54-524f-505353415031
Device Code  01000045-444f-4354-524f-505353415031
Package Ctrl 01000043-474b-5054-524f-505353415031
Package Data 01000044-474b-5054-524f-505353415031
Package Stat 01000053-474b-5054-524f-505353415031
```

开始控制包为 little-endian：`op:u8=1 + total_size:u32 + crc32:u32 + target_id:u64`。系统比较 `target_id` 后才打开 staging 文件。数据写入 Package Data，建议每片 180 B。结束向 Package Ctrl 写单字节 `0x02`。

NimBLE 回调只复制小分片到固定队列；文件写入、CRC 和 `.pap` 安装在 `pap_install` 工作任务执行。

## 安全边界

- 不使用 BLE 系统配对/绑定。
- 不隐藏设备码，不把设备码视为密码。
- V1 不解决窃听或恶意写入；产品目标仅是减少发送到错误设备的概率。
- Package Service 仍执行路径穿越拦截、尺寸限制和 CRC，避免损坏文件破坏目录。
