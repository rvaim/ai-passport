#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import importlib.util
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "ble_install", ROOT / "tools" / "ble_install.py")
ble_install = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(ble_install)

DEVICE_CODE = "4E48R-53GD6-Z"


class FakeScanner:
    @staticmethod
    async def find_device_by_filter(predicate, timeout):
        assert timeout == 12.0
        device = types.SimpleNamespace(name=f"Passport-{DEVICE_CODE}")
        advertisement = types.SimpleNamespace(local_name=device.name)
        return device if predicate(device, advertisement) else None


class FakeClient:
    final_status = "安装成功"
    latest = None

    def __init__(self, device):
        self.device = device
        self.writes = []
        self.status_callback = None
        FakeClient.latest = self

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_args):
        return False

    async def read_gatt_char(self, uuid):
        assert uuid == ble_install.CODE_UUID
        return DEVICE_CODE.encode()

    async def start_notify(self, uuid, callback):
        assert uuid == ble_install.PKG_STATUS_UUID
        self.status_callback = callback

    async def stop_notify(self, uuid):
        assert uuid == ble_install.PKG_STATUS_UUID

    async def write_gatt_char(self, uuid, value, response):
        payload = bytes(value)
        self.writes.append((uuid, payload, response))
        if uuid == ble_install.PKG_CTRL_UUID and payload[0] == 1:
            self.status_callback(None, bytearray("开始接收".encode()))
        elif uuid == ble_install.PKG_CTRL_UUID and payload == b"\x02":
            self.status_callback(None, bytearray(self.final_status.encode()))


fake_bleak = types.ModuleType("bleak")
fake_bleak.BleakClient = FakeClient
fake_bleak.BleakScanner = FakeScanner
sys.modules["bleak"] = fake_bleak

assert ble_install.normalize_code("4e48r53gd6z") == DEVICE_CODE

with tempfile.TemporaryDirectory() as temp_dir:
    package = Path(temp_dir) / "test.pap"
    package.write_bytes(bytes(range(256)) * 2)
    asyncio.run(ble_install.install(DEVICE_CODE, package))
    data_writes = [write for write in FakeClient.latest.writes
                   if write[0] == ble_install.PKG_DATA_UUID]
    assert len(data_writes) == 3
    assert all(response is True for _, _, response in data_writes)

    FakeClient.final_status = "安装失败"
    try:
        asyncio.run(ble_install.install(DEVICE_CODE, package))
    except RuntimeError as error:
        assert "安装失败" in str(error)
    else:
        raise AssertionError("device installation failure was ignored")

print("BLE installer host tests: PASS")
