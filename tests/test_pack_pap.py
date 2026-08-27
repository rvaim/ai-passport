#!/usr/bin/env python3
from __future__ import annotations
import importlib.util
import json
import struct
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack_pap", ROOT / "tools" / "pack_pap.py")
pack_pap = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(pack_pap)


def expect_value_error(manifest: dict) -> None:
    try:
        pack_pap.validate_manifest(manifest)
    except ValueError:
        return
    raise AssertionError("invalid manifest was accepted")


try:
    json.loads('{"type":"app","type":"theme"}',
               object_pairs_hook=pack_pap.exact_object)
except ValueError:
    pass
else:
    raise AssertionError("duplicate JSON field was accepted")


with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    source = root / "app"
    source.mkdir()
    (source / "manifest.json").write_text(json.dumps({
        "type": "app", "id": "com.example.test", "name": "测试",
        "version": "1.0.0", "api": 1, "runtime": "lua", "entry": "main.lua"
    }, ensure_ascii=False), encoding="utf-8")
    (source / "main.lua").write_text('print("ok")\n', encoding="utf-8")
    (source / "README.md").write_text("not shipped", encoding="utf-8")
    (source / ".DS_Store").write_bytes(b"finder metadata")
    hidden = source / ".cache"
    hidden.mkdir()
    (hidden / "index").write_bytes(b"tool metadata")
    out = root / "test.pap"
    pack_pap.build(source, out)
    data = out.read_bytes()
    magic, version, kind, mlen, count = struct.unpack_from("<4sHHII", data, 0)
    assert magic == b"PAP1" and version == 1 and kind == 1 and count == 1
    manifest = json.loads(data[16:16+mlen].decode("utf-8"))
    assert manifest["id"] == "com.example.test"
    assert pack_pap.safe_rel("assets/icon.bin")
    assert not pack_pap.safe_rel("../secret")
    assert not pack_pap.safe_rel("/absolute")
    assert not pack_pap.safe_rel("a\\b")
    assert not pack_pap.safe_rel("资源/icon.bin")
    assert not pack_pap.safe_rel("a//b")
    assert not pack_pap.safe_rel("a/")

theme = {
    "type": "theme", "id": "theme.test", "name": "Test theme",
    "version": "1.0.0", "api": 1,
    "tokens": {
        "background": "#112233", "surface": "#223344",
        "item_background": "#445566", "text": "#FFFFFF",
        "muted_text": "#AABBCC", "accent": "#3366CC",
        "selection_text": "#FFFFFF", "divider": "#111111",
        "border": "#010203", "shadow": "#000000",
        "spacing": 8, "radius": 32, "border_width": 4, "shadow_width": 12,
        "shadow_spread": 6, "shadow_opacity": 255,
        "shadow_offset_x": -8, "shadow_offset_y": 8,
    },
}
pack_pap.validate_manifest(theme)

bad = {**theme, "tokens": {**theme["tokens"], "shadow_blur": 4}}
expect_value_error(bad)
bad = {**theme, "tokens": {**theme["tokens"], "radius": 33}}
expect_value_error(bad)
bad = {**theme, "tokens": {**theme["tokens"], "border_width": 1.5}}
expect_value_error(bad)
bad = {**theme, "tokens": {**theme["tokens"], "shadow": "black"}}
expect_value_error(bad)
bad = {**theme, "tokens": {**theme["tokens"], "background": "112233"}}
expect_value_error(bad)
bad = {key: value for key, value in theme.items() if key != "tokens"}
expect_value_error(bad)
bad = {**theme, "tokens": {key: value for key, value in theme["tokens"].items()
                           if key != "surface"}}
expect_value_error(bad)
expect_value_error({**theme, "api": 2})
expect_value_error({**theme, "api": 1.0})
expect_value_error({**theme, "legacy": True})

app = {
    "type": "app", "id": "com.example.strict", "name": "Strict",
    "version": "1.0.0", "api": 1, "runtime": "lua", "entry": "main.lua",
}
pack_pap.validate_manifest(app)
expect_value_error({**app, "permissions": ["ui"]})
expect_value_error({**app, "api": 2})
expect_value_error({**app, "api": True})
expect_value_error({**app, "name": "bad\0name"})

print("PAP packer host tests: PASS")
