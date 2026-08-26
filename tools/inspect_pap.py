#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, struct, zlib
from pathlib import Path

HEADER = struct.Struct("<4sHHII")
ENTRY = struct.Struct("<HHII")


def inspect(path: Path) -> None:
    with path.open("rb") as f:
        magic, version, kind, mlen, count = HEADER.unpack(f.read(HEADER.size))
        if magic != b"PAP1" or version != 1 or kind not in (1, 2):
            raise SystemExit("无效 .pap 头")
        manifest_raw = f.read(mlen)
        manifest = json.loads(manifest_raw.decode("utf-8"))
        print(json.dumps(manifest, ensure_ascii=False, indent=2))
        print(f"类型: {'app' if kind == 1 else 'theme'}; 文件: {count}")
        for _ in range(count):
            path_len, flags, size, expected = ENTRY.unpack(f.read(ENTRY.size))
            rel = f.read(path_len).decode("utf-8")
            data = f.read(size)
            actual = zlib.crc32(data) & 0xFFFFFFFF
            if actual != expected:
                raise SystemExit(f"CRC 错误: {rel}")
            print(f"  {rel}: {size} 字节, CRC={actual:08x}, flags={flags}")
        if f.read(1):
            raise SystemExit("包尾存在多余数据")
    print("检查通过")


def main() -> None:
    p = argparse.ArgumentParser(description="检查 Passport .pap 包")
    p.add_argument("package", type=Path)
    args = p.parse_args()
    inspect(args.package)

if __name__ == "__main__": main()
