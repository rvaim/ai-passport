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
包含 `type`、`id`、`name`、`version`、`api`、`styles`。API 必须是当前版本 `1`；
缺失、重复或未知字段都会被拒绝。主题还必须在提交安装前通过完整的
[有界公共样式 Schema](theme-system.zh_CN.md)。

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

安装时先写入 `.staging/app-<id>` 或 `.staging/theme-<id>`，校验所有 Entry 和 App
入口文件后才对外发布。已安装 App 使用 `apps/<id>/bundle` 保存 Manifest 与 payload，
使用 `apps/<id>/data` 保存由系统管理的持久化数据。更新只替换 `bundle` 并保留 `data`；
卸载先把整个 App 容器原子移动到 `.trash`，再递归删除代码与数据。失败路径会尽量恢复
原 bundle；启动时还会恢复被断电中断的 App/主题交换，并删除未发布的 staging/incoming
文件。这是事务安全机制，不是旧格式兼容层。

系统“主题”App 使用同一个存储工作任务卸载主题。内置 `default` 主题受保护；删除当前
已安装主题时，系统会先持久化 `default`，再把主题目录移动到 `.trash`。卸载主题不会
影响插件的私有 `data` 子树。
