#!/usr/bin/env python3
"""Build a Passport App Package (.pap) without compression or whole-file buffering on device."""
from __future__ import annotations

import argparse
import json
import re
import struct
import zlib
from pathlib import Path

MAGIC = b"PAP1"
VERSION = 1
API_VERSION = 1
MANIFEST_MAX_BYTES = 4096
PATH_MAX_BYTES = 120
MAX_ENTRIES = 64
ENTRY_MAX_BYTES = 4 * 1024 * 1024
PACKAGE_MAX_BYTES = 4 * 1024 * 1024
KIND = {"app": 1, "theme": 2}
HEADER = struct.Struct("<4sHHII")
ENTRY = struct.Struct("<HHII")
THEME_COLOR_TOKENS = {
    "background", "surface", "item_background", "text", "muted_text", "accent",
    "selection_text", "divider", "border", "shadow",
}
THEME_INTEGER_TOKENS = {
    "spacing": (2, 12),
    "radius": (0, 32),
    "border_width": (0, 4),
    "shadow_width": (0, 12),
    "shadow_spread": (0, 6),
    "shadow_opacity": (0, 255),
    "shadow_offset_x": (-8, 8),
    "shadow_offset_y": (-8, 8),
}
THEME_COLOR_PATTERN = re.compile(r"#[0-9A-Fa-f]{6}")


def exact_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"JSON 对象包含重复字段: {key}")
        result[key] = value
    return result


def safe_rel(path: str) -> bool:
    portable = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/")
    return (
        isinstance(path, str)
        and bool(path)
        and not path.startswith("/")
        and "\\" not in path
        and all(ch in portable for ch in path)
        and all(segment not in ("", ".", "..") for segment in path.split("/"))
    )


def validate_manifest(m: dict) -> None:
    if not isinstance(m, dict):
        raise ValueError("manifest 必须是 JSON 对象")
    if m.get("type") not in KIND:
        raise ValueError("type 必须是 app 或 theme")
    expected = ({"type", "id", "name", "version", "api", "runtime", "entry"}
                if m["type"] == "app" else
                {"type", "id", "name", "version", "api", "tokens"})
    missing = sorted(expected - set(m))
    unknown = sorted(set(m) - expected)
    if missing:
        raise ValueError(f"manifest 缺少字段: {', '.join(missing)}")
    if unknown:
        raise ValueError(f"manifest 包含未支持字段: {', '.join(unknown)}")
    if (not isinstance(m["id"], str) or not m["id"] or len(m["id"]) >= 48 or
            any(c not in "abcdefghijklmnopqrstuvwxyz0123456789._-" for c in m["id"])):
        raise ValueError("id 只能包含小写字母、数字、点、下划线和连字符")
    if (not isinstance(m["name"], str) or not m["name"] or
            "\0" in m["name"] or
            len(m["name"].encode("utf-8")) >= 48):
        raise ValueError("name 必须是少于 48 字节的非空 UTF-8 字符串")
    if (not isinstance(m["version"], str) or not m["version"] or
            "\0" in m["version"] or
            len(m["version"].encode("utf-8")) >= 20):
        raise ValueError("version 必须是少于 20 字节的非空 UTF-8 字符串")
    if (isinstance(m["api"], bool) or not isinstance(m["api"], int) or
            m["api"] != API_VERSION):
        raise ValueError(f"api 必须是当前版本 {API_VERSION}")
    if m["type"] == "app":
        if m["runtime"] != "lua":
            raise ValueError("app runtime 必须是 lua")
        if (not isinstance(m["entry"], str) or not safe_rel(m["entry"]) or
                len(m["entry"].encode("ascii")) >= 96):
            raise ValueError("app 必须提供少于 96 字节的安全 entry 相对路径")
    if m["type"] == "theme":
        tokens = m["tokens"]
        if not isinstance(tokens, dict):
            raise ValueError("theme 必须提供 tokens 对象")
        known = THEME_COLOR_TOKENS | set(THEME_INTEGER_TOKENS)
        missing = sorted(known - set(tokens))
        unknown = sorted(set(tokens) - known)
        if missing:
            raise ValueError(f"主题缺少 token: {', '.join(missing)}")
        if unknown:
            raise ValueError(f"不支持的主题 token: {', '.join(unknown)}")
        for name in THEME_COLOR_TOKENS:
            value = tokens[name]
            if not isinstance(value, str) or not THEME_COLOR_PATTERN.fullmatch(value):
                raise ValueError(f"主题颜色 {name} 必须是 #RRGGBB")
        for name, (minimum, maximum) in THEME_INTEGER_TOKENS.items():
            value = tokens[name]
            if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
                raise ValueError(f"主题数值 {name} 必须是 {minimum}..{maximum} 的整数")


def build(source: Path, output: Path) -> None:
    manifest_path = source / "manifest.json"
    manifest = json.loads(
        manifest_path.read_text(encoding="utf-8"), object_pairs_hook=exact_object)
    validate_manifest(manifest)
    manifest_bytes = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(manifest_bytes) > MANIFEST_MAX_BYTES:
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
        if not safe_rel(rel) or len(rel.encode()) >= PATH_MAX_BYTES:
            raise ValueError(f"不安全或过长的包路径: {rel}")
        if path.stat().st_size > ENTRY_MAX_BYTES:
            raise ValueError(f"包文件超过 4 MiB: {rel}")
        files.append((rel, path))

    if len(files) > MAX_ENTRIES:
        raise ValueError(f"payload 文件超过 {MAX_ENTRIES} 个")
    if manifest["type"] == "app" and manifest["entry"] not in {rel for rel, _ in files}:
        raise ValueError("app entry 不存在，或被打包过滤规则排除")

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
    if output.stat().st_size > PACKAGE_MAX_BYTES:
        output.unlink()
        raise ValueError(".pap 超过 4 MiB BLE 传输上限")
    print(f"已生成 {output} ({output.stat().st_size} 字节, {len(files)} 个资源文件)")


def main() -> None:
    parser = argparse.ArgumentParser(description="打包 Passport .pap 插件或主题")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.source.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
