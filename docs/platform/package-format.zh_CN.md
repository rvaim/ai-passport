<p align="right"><a href="package-format.md">English</a> · <strong>简体中文</strong></p>

# `.pap` 包格式 v1

`.pap` 是面向低 RAM MCU 的顺序小端包，不使用 ZIP；设备会逐个校验并写入 Entry，
不需要把整个包放入内存。

## Header

16 字节 Header 后紧跟 UTF-8 JSON Manifest。

```text
magic[4] = PAP1
format_version:u16 = 1
kind:u16 = 1(app) / 2(theme)
manifest_len:u32
entry_count:u32
```

Header kind 必须与 Manifest `type` 一致。App Manifest 只能且必须包含 `type`、
`id`、`name`、`version`、`api`、`runtime`、`entry`；Theme Manifest 只能且必须
包含 `type`、`id`、`name`、`version`、`api`、`tokens`。API 必须是当前版本 `1`；
缺失、重复或未知字段都会被拒绝。主题还必须在提交安装前通过完整的
[18 Token Schema](theme-system.zh_CN.md)。

## 文件 Entry

```text
path_len:u16
flags:u16 = 0
size:u32
crc32:u32
path[path_len]
data[size]
```

路径必须是相对 ASCII，只允许字母、数字和 `._-/`。绝对路径，空、`.`、`..`
路径段，反斜杠，重复路径，把 `manifest.json` 作为 payload，非零 flags，CRC
错误以及包尾多余字节都会被拒绝。显示名称和 Manifest 文本仍可使用 UTF-8 中文。

当前上限为：Manifest 4096 字节、64 个 payload Entry、路径少于 120 字节、单个
Entry 最大 4 MiB；Passport Link 还会把整个传输包限制在 4 MiB。

安装时先写入 `.staging/<id>`，校验所有 Entry 和 App 入口文件，再通过临时 backup
原子替换已安装目录。失败路径会尽量恢复原目录；这是事务安全机制，不是旧格式兼容层。
