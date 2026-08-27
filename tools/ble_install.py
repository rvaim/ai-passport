#!/usr/bin/env python3
"""Install a .pap package over Passport Link BLE. Requires: pip install bleak."""
from __future__ import annotations
import argparse
import asyncio
import struct
import zlib
from pathlib import Path

SERVICE_UUID = "0100004b-4e49-4c54-524f-505353415031"
CODE_UUID = "01000045-444f-4354-524f-505353415031"
PKG_CTRL_UUID = "01000043-474b-5054-524f-505353415031"
PKG_DATA_UUID = "01000044-474b-5054-524f-505353415031"
PKG_STATUS_UUID = "01000053-474b-5054-524f-505353415031"
ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
MAX_PACKAGE_SIZE = 4 * 1024 * 1024
PACKAGE_CHUNK_SIZE = 180
FAILURE_STATUSES = {"设备码不匹配", "无法写入存储", "写入失败", "安装失败"}


def parse_code(code: str) -> int:
    compact = "".join(code.upper().split()).replace("-", "")
    if len(compact) != 11:
        raise ValueError("设备码格式应为 XXXXX-XXXXX-X")
    value = 0
    check = 0
    for i, char in enumerate(compact[:10]):
        digit = ALPHABET.find(char)
        if digit < 0:
            raise ValueError("设备码包含无效字符")
        value = (value << 5) | digit
        check = (check + (i + 1) * digit) % 32
    if ALPHABET.find(compact[10]) != check or value > 0xFFFFFFFFFFFF:
        raise ValueError("设备码校验失败")
    return value


def normalize_code(code: str) -> str:
    parse_code(code)
    compact = "".join(code.upper().split()).replace("-", "")
    return f"{compact[:5]}-{compact[5:10]}-{compact[10]}"


async def wait_for_status(queue: asyncio.Queue[str], expected: str,
                          timeout: float, timeout_message: str) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            raise RuntimeError(timeout_message)
        try:
            status = await asyncio.wait_for(queue.get(), remaining)
        except TimeoutError as exc:
            raise RuntimeError(timeout_message) from exc
        if status in FAILURE_STATUSES:
            raise RuntimeError(f"设备返回：{status}")
        if status == expected:
            return


async def install(code: str, package: Path) -> None:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        raise SystemExit("缺少 bleak，请先执行: pip install bleak") from exc

    canonical_code = normalize_code(code)
    device_id = parse_code(canonical_code)
    wanted_name = f"Passport-{canonical_code}"
    print(f"正在查找 {wanted_name} ...")
    device = await BleakScanner.find_device_by_filter(
        lambda _device, advertisement: advertisement.local_name == wanted_name,
        timeout=12.0)
    if not device:
        raise SystemExit("未找到目标设备，请确认设备码及蓝牙距离")

    if (package.suffix.lower() != ".pap" or not package.is_file() or
            package.stat().st_size == 0 or package.stat().st_size > MAX_PACKAGE_SIZE):
        raise RuntimeError("安装文件必须是 1..4194304 字节的 .pap")
    data = package.read_bytes()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    status_queue: asyncio.Queue[str] = asyncio.Queue()
    loop = asyncio.get_running_loop()

    def status_cb(_, value: bytearray) -> None:
        text = bytes(value).decode("utf-8", errors="replace").strip()
        loop.call_soon_threadsafe(status_queue.put_nowait, text)
        print(f"设备: {text}")

    async with BleakClient(device) as client:
        actual_code = normalize_code(
            (await client.read_gatt_char(CODE_UUID)).decode("utf-8"))
        if actual_code != canonical_code:
            raise SystemExit(f"目标复核失败：设备报告 {actual_code}")
        await client.start_notify(PKG_STATUS_UUID, status_cb)
        try:
            begin = struct.pack("<BIIQ", 1, len(data), crc, device_id)
            await client.write_gatt_char(PKG_CTRL_UUID, begin, response=True)
            await wait_for_status(
                status_queue, "开始接收", 6.0, "设备没有确认开始接收")

            # Acknowledged writes provide backpressure for the device's bounded
            # eight-entry queue. Unacknowledged bursts can silently drop chunks.
            for offset in range(0, len(data), PACKAGE_CHUNK_SIZE):
                chunk = data[offset:offset + PACKAGE_CHUNK_SIZE]
                await client.write_gatt_char(PKG_DATA_UUID, chunk, response=True)

            await client.write_gatt_char(PKG_CTRL_UUID, b"\x02", response=True)
            await wait_for_status(
                status_queue, "安装成功", 120.0, "等待设备安装结果超时")
        finally:
            await client.stop_notify(PKG_STATUS_UUID)
    print(f"安装完成: {package.name}")


def main() -> None:
    p = argparse.ArgumentParser(description="通过无系统配对 BLE 安装 Passport .pap")
    p.add_argument("device_code", help="例如 XXXXX-XXXXX-X")
    p.add_argument("package", type=Path)
    args = p.parse_args()
    try:
        asyncio.run(install(args.device_code, args.package.resolve()))
    except (OSError, RuntimeError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()
