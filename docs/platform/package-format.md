<p align="right"><strong>English</strong> · <a href="package-format.zh_CN.md">简体中文</a></p>

# `.pap` Package Format v1

`.pap` is a sequential little-endian package for bounded-RAM streaming. It is
not ZIP: the device validates and writes one entry at a time.

## Header

The 16-byte header is followed immediately by the UTF-8 JSON manifest.

```text
magic[4] = PAP1
format_version:u16 = 1
kind:u16 = 1 (app) / 2 (theme)
manifest_len:u32
entry_count:u32
```

The header kind and manifest `type` must match. App manifests contain exactly
`type`, `id`, `name`, `version`, `api`, `runtime`, and `entry`. Theme manifests
contain exactly `type`, `id`, `name`, `version`, `api`, and `tokens`. API must
be the current version `1`; missing, duplicate, and unknown fields are rejected.
Themes must also pass the complete [18-token schema](theme-system.md) before
installation is committed.

## File entry

```text
path_len:u16
flags:u16 = 0
size:u32
crc32:u32
path[path_len]
data[size]
```

Paths are relative ASCII using only letters, digits, and `._-/`. Absolute
paths, empty/`.`/`..` segments, backslashes, duplicate paths, `manifest.json`
as a payload, nonzero flags, CRC mismatches, and trailing package bytes are
rejected. Display names and manifest text may still use UTF-8 Chinese.

Current bounds are a 4096-byte manifest, 64 payload entries, paths shorter than
120 bytes, and 4 MiB per entry. Passport Link also limits the complete package
transfer to 4 MiB.

Installation writes `.staging/<id>`, validates every entry and the App entry
file, then atomically renames the installed directory through a temporary
backup. Failure paths restore the prior directory where possible; this is a
transaction safety mechanism, not a legacy-format compatibility layer.
