#!/usr/bin/env python3
"""Pack every example PAP and generate the catalog consumed by the web site."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
HEADER = struct.Struct("<4sHHII")
KIND_NAMES = {1: "app", 2: "theme"}
PAP_MAGIC = b"PAP1"
PAP_VERSION = 1


def display_path(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def load_packer():
    path = ROOT / "tools" / "pack_pap.py"
    spec = importlib.util.spec_from_file_location("passport_pack_pap", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"无法加载打包器: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACKER = load_packer()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=PACKER.exact_object
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"无法读取 {display_path(path)}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{display_path(path)}: Manifest 必须是 JSON 对象")
    return value


def read_pap_manifest(path: Path) -> tuple[str, dict[str, Any]]:
    try:
        with path.open("rb") as package:
            header = package.read(HEADER.size)
            if len(header) != HEADER.size:
                raise ValueError("包头长度不足")
            magic, version, kind, manifest_length, entry_count = HEADER.unpack(header)
            if magic != PAP_MAGIC or version != PAP_VERSION:
                raise ValueError("包头版本或魔数无效")
            if (kind not in KIND_NAMES or manifest_length == 0 or
                    manifest_length > PACKER.MANIFEST_MAX_BYTES):
                raise ValueError("包类型或 Manifest 长度无效")
            if entry_count > PACKER.MAX_ENTRIES:
                raise ValueError("Entry 数量超出上限")
            encoded = package.read(manifest_length)
            if len(encoded) != manifest_length:
                raise ValueError("Manifest 内容不足")
        manifest = json.loads(encoded.decode("utf-8"), object_pairs_hook=PACKER.exact_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, struct.error, ValueError) as error:
        raise ValueError(f"无法读取 {display_path(path)}: {error}") from error
    if not isinstance(manifest, dict):
        raise ValueError(f"{display_path(path)}: 包内 Manifest 必须是 JSON 对象")
    return KIND_NAMES[kind], manifest


def readme_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")


def readme_heading(text: str, fallback: str) -> str:
    for line in text.splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def readme_description(text: str) -> str:
    lines = text.splitlines()
    paragraph: list[str] = []
    started = False
    for line in lines:
        stripped = line.strip()
        if not started:
            if not stripped or stripped.startswith("<p") or stripped.startswith("#"):
                continue
            if stripped.startswith("```") or stripped.startswith(">"):
                continue
            started = True
        if not stripped:
            if paragraph:
                break
            continue
        if stripped.startswith("#") or stripped.startswith("```"):
            break
        paragraph.append(stripped)
    value = " ".join(paragraph)
    value = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", value)
    value = re.sub(r"[*_`]+", "", value)
    value = re.sub(r"(?<=[\u3400-\u4dbf\u4e00-\u9fff])\s+(?=[\u3400-\u4dbf\u4e00-\u9fff])", "", value)
    return value


def package_asset_name(kind: str, package_id: str) -> str:
    return f"{kind}-{package_id}.pap"


def find_package(source: Path, manifest: dict[str, Any]) -> Path | None:
    dist = source / "dist"
    candidates = sorted(dist.glob("*.pap")) if dist.is_dir() else []
    matches: list[Path] = []
    for candidate in candidates:
        kind, packaged = read_pap_manifest(candidate)
        if kind == manifest["type"] and packaged.get("id") == manifest.get("id"):
            matches.append(candidate)
    if len(matches) > 1:
        names = ", ".join(path.name for path in matches)
        raise ValueError(
            f"{display_path(source)}: 同一 Manifest 对应多个 PAP: {names}"
        )
    if matches:
        return matches[0]
    if candidates:
        names = ", ".join(path.name for path in candidates)
        raise ValueError(
            f"{display_path(source)}: dist 中的 PAP 与 Manifest 不匹配: {names}"
        )
    return None


def packed_bytes(source: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="passport-pap-") as temporary:
        output = Path(temporary) / "package.pap"
        with contextlib.redirect_stdout(io.StringIO()):
            PACKER.build(source, output)
        return output.read_bytes()


def update_package(path: Path, data: bytes, write: bool) -> None:
    if path.is_file() and path.read_bytes() == data:
        return
    if not write:
        raise ValueError(f"{display_path(path)} 不是当前源码生成的最新 PAP")
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb", prefix=f".{path.name}.", suffix=".tmp", dir=path.parent, delete=False
    ) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    temporary_path.replace(path)


def build_catalog(
    root: Path, *, write: bool, prune: bool, packages_dir: Path | None
) -> dict[str, Any]:
    manifests = sorted((root / "examples").glob("**/manifest.json"))
    entries: list[dict[str, Any]] = []
    managed_dist: set[Path] = set()
    seen_assets: set[str] = set()

    for manifest_path in manifests:
        source = manifest_path.parent
        manifest = read_json(manifest_path)
        try:
            PACKER.validate_manifest(manifest)
        except ValueError as error:
            raise ValueError(f"{display_path(manifest_path)}: {error}") from error

        package_path = find_package(source, manifest)
        if package_path is None:
            package_path = source / "dist" / f"{source.name}.pap"
        data = packed_bytes(source)
        update_package(package_path, data, write)
        managed_dist.add(package_path.resolve())

        source_rel = source.relative_to(root).as_posix()
        readme = source / "README.md"
        readme_zh = source / "README.zh_CN.md"
        english = readme_text(readme)
        chinese = readme_text(readme_zh)
        package_rel = package_path.relative_to(root).as_posix()
        asset = package_asset_name(manifest["type"], manifest["id"])
        if asset in seen_assets:
            raise ValueError(f"重复的 PAP 标识: {asset}")
        seen_assets.add(asset)
        digest = hashlib.sha256(data).hexdigest()
        entry = {
            "id": manifest["id"],
            "type": manifest["type"],
            "name": manifest["name"],
            "name_en": readme_heading(english, manifest["name"]),
            "version": manifest["version"],
            "api": manifest["api"],
            "runtime": manifest.get("runtime"),
            "description": readme_description(english),
            "description_zh": readme_description(chinese),
            "source": source_rel,
            "readme": readme.relative_to(root).as_posix() if readme.is_file() else None,
            "readme_zh": readme_zh.relative_to(root).as_posix()
            if readme_zh.is_file()
            else None,
            "package": package_rel,
            "asset": asset,
            "size": len(data),
            "sha256": digest,
        }
        entries.append(entry)

        if packages_dir is not None:
            packages_dir.mkdir(parents=True, exist_ok=True)
            (packages_dir / asset).write_bytes(data)

    if prune:
        orphaned = [
            candidate
            for candidate in sorted((root / "examples").glob("**/dist/*.pap"))
            if candidate.resolve() not in managed_dist
        ]
        if orphaned and not write:
            names = ", ".join(candidate.relative_to(root).as_posix() for candidate in orphaned)
            raise ValueError(f"dist 中存在未登记的孤立 PAP: {names}")
        for candidate in orphaned:
            candidate.unlink()
            print(f"删除孤立 PAP: {candidate.relative_to(root)}")

    entries.sort(key=lambda item: (item["type"], item["id"]))
    return {
        "schema": 1,
        "packages": entries,
    }


def write_catalog(path: Path, catalog: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 Passport 插件/主题 PAP 目录")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="重新生成并写入 dist/*.pap")
    mode.add_argument("--check", action="store_true", help="检查 dist/*.pap 是否最新")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--prune", action="store_true", help="删除 examples 下孤立的 PAP")
    parser.add_argument("--catalog", type=Path, help="写入 catalog.json")
    parser.add_argument("--packages-dir", type=Path, help="复制用于发布的 PAP 文件")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        catalog = build_catalog(
            root,
            write=args.write,
            prune=args.prune,
            packages_dir=args.packages_dir.resolve() if args.packages_dir else None,
        )
        if args.catalog:
            write_catalog(args.catalog.resolve(), catalog)
        print(f"PAP catalog: PASS ({len(catalog['packages'])} packages)")
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
