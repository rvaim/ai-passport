<p align="right"><a href="package-format.md">English</a> · <strong>简体中文</strong></p>

# `.pap` 包格式 v1

`.pap` 是面向低 RAM MCU 的顺序二进制包，不使用 ZIP，设备可以边读边写。

## Header（16 B，小端）

```text
magic[4] = PAP1
format_version:u16 = 1
kind:u16 = 1(app) / 2(theme)
manifest_len:u32
entry_count:u32
```

紧接 UTF-8 JSON manifest。

## 文件 Entry

```text
path_len:u16
flags:u16 = 0
size:u32
crc32:u32
path[path_len]
data[size]
```

路径不能是绝对路径、不能包含 `..`、`.` 空段或反斜杠；payload 文件路径仅允许 ASCII 字母、数字、`._-/`，避免 FAT codepage 差异。Manifest 中的插件名称和界面文案仍可使用 UTF-8 中文。manifest 最大 4096 B，单包最多 64 个 payload entry。

安装过程：读 header/manifest → 建 `.staging/<id>` → 流式写每个文件并校验 CRC → 检查 App entry 存在 → 旧版本改名 backup → staging 改名 final → 删除 backup。断电原子性不是数据库级别，但失败路径会尽量回滚旧版本。
