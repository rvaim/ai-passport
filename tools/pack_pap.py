#!/usr/bin/env python3
"""Build a Passport App Package (.pap) without compression or whole-file buffering on device."""
from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

MAGIC = b"PAP1"
VERSION = 1
KIND = {"app": 1, "theme": 2}
HEADER = struct.Struct("<4sHHII")
ENTRY = struct.Struct("<HHII")


def safe_rel(path: str) -> bool:
    p = Path(path)
    portable = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/")
    return (
        bool(path)
        and not p.is_absolute()
        and "\\" not in path
        and all(ch in portable for ch in path)
        and all(x not in ("", ".", "..") for x in p.parts)
    )


def validate_manifest(m: dict) -> None:
    required = ("type", "id", "name", "version", "api")
    for key in required:
        if key not in m:
            raise ValueError(f"manifest 缺少字段: {key}")
    if m["type"] not in KIND:
        raise ValueError("type 必须是 app 或 theme")
    if not m["id"] or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789._-" for c in m["id"]):
        raise ValueError("id 只能包含小写字母、数字、点、下划线和连字符")
    if m["type"] == "app" and ("entry" not in m or not safe_rel(m["entry"])):
        raise ValueError("app 必须提供安全的 entry 相对路径")
    if not isinstance(m["api"], int) or m["api"] < 1:
        raise ValueError("api 必须是 >=1 的整数")


def build(source: Path, output: Path) -> None:
    manifest_path = source / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_manifest(manifest)
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(manifest_bytes) > 4096:
        raise ValueError("manifest 超过 4096 字节")

    files: list[tuple[str, Path]] = []
    for path in sorted(source.rglob("*")):
        rel_parts = path.relative_to(source).parts
        if (not path.is_file() or path == manifest_path or
                any(part.startswith(".") for part in rel_parts) or
                "dist" in rel_parts or
                path.name in ("README.md", "README.zh_CN.md")):
            continue
        rel = path.relative_to(source).as_posix()
        if not safe_rel(rel) or len(rel.encode()) >= 120:
            raise ValueError(f"不安全或过长的包路径: {rel}")
        files.append((rel, path))

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as out:
        out.write(HEADER.pack(MAGIC, VERSION, KIND[manifest["type"]], len(manifest_bytes), len(files)))
        out.write(manifest_bytes)
        for rel, path in files:
            data = path.read_bytes()
            rel_bytes = rel.encode("utf-8")
            out.write(ENTRY.pack(len(rel_bytes), 0, len(data), zlib.crc32(data) & 0xFFFFFFFF))
            out.write(rel_bytes)
            out.write(data)
    print(f"已生成 {output} ({output.stat().st_size} 字节, {len(files)} 个资源文件)")


def main() -> None:
    parser = argparse.ArgumentParser(description="打包 Passport .pap 插件或主题")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.source.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
