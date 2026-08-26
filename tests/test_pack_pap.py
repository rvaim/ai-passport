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

print("PAP packer host tests: PASS")
