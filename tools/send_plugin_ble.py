#!/usr/bin/env python3
"""Upload a signed Passport .fpp package over unpaired BLE GATT."""

from __future__ import annotations

import argparse
import asyncio
import re
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "f0771000-6f6c-6f74-6f79-70617373706f"
CONTROL_UUID = "f0771001-6f6c-6f74-6f79-70617373706f"
DATA_UUID = "f0771002-6f6c-6f74-6f79-70617373706f"
STATUS_UUID = "f0771003-6f6c-6f74-6f79-70617373706f"

STATE_NAMES = (
    "idle",
    "receiving",
    "verifying",
    "waiting for physical approval",
    "installing",
    "complete",
    "error",
)


@dataclass(frozen=True)
class Status:
    state: int
    sync: int
    error: int
    expected: int
    received: int
    plugin_version: int

    @property
    def name(self) -> str:
        return STATE_NAMES[self.state] if self.state < len(STATE_NAMES) else "unknown"


def parse_status(raw: bytes) -> Status:
    if len(raw) != 20 or raw[0] != 2:
        raise RuntimeError(f"unsupported BLE status payload: {raw.hex()}")
    state = raw[1]
    sync = raw[2]
    error, expected, received, version = struct.unpack_from("<iIII", raw, 4)
    return Status(state, sync, error, expected, received, version)


async def read_status(client: BleakClient) -> Status:
    return parse_status(bytes(await client.read_gatt_char(STATUS_UUID)))


async def wait_for(
    client: BleakClient, states: set[int], timeout: float, description: str
) -> Status:
    deadline = time.monotonic() + timeout
    last: Status | None = None
    while time.monotonic() < deadline:
        current = await read_status(client)
        if current != last:
            print(
                f"{current.name}: {current.received}/{current.expected} bytes"
                + (f", error={current.error}" if current.error else "")
            )
            last = current
        if current.state in states:
            return current
        await asyncio.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {description}")


async def find_device(selector: str | None, timeout: float):
    print("scanning for Passport installer...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for device, advertisement in devices.values():
        name = advertisement.local_name or device.name or ""
        advertised = {value.lower() for value in advertisement.service_uuids}
        if selector and selector not in (device.address, name):
            continue
        if SERVICE_UUID in advertised or name.startswith("Passport-"):
            print(f"found {name or '(unnamed)'} [{device.address}]")
            return device
    target = f" matching {selector!r}" if selector else ""
    raise RuntimeError(f"no Passport installer{target} found")


def normalize_device_code(value: str) -> str:
    compact = re.sub(r"[-\s]", "", value.upper())
    if not re.fullmatch(r"[0-9A-HJKMNP-TV-Z]{10}", compact):
        raise ValueError("device code must be the ten characters shown on the device")
    return compact


async def synchronize(client: BleakClient, device_code: str) -> None:
    await client.write_gatt_char(
        CONTROL_UUID, b"\x10" + device_code.encode("ascii"), response=True
    )
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        status = await read_status(client)
        if status.sync == 2:
            print("device code matched; session synchronized")
            return
        if status.sync == 3:
            raise RuntimeError("device code does not match the connected Passport")
        await asyncio.sleep(0.05)
    raise TimeoutError("timed out waiting for device-code synchronization")


async def upload(arguments: argparse.Namespace) -> None:
    package = arguments.package.read_bytes()
    device_code = normalize_device_code(arguments.device_code)
    if len(package) < 108 or len(package) > 0x3F000:
        raise ValueError("package size is outside the firmware limit")
    if arguments.chunk_size <= 0:
        raise ValueError("chunk size must be positive")
    device = await find_device(arguments.device, arguments.scan_timeout)

    async with BleakClient(device, timeout=arguments.connect_timeout) as client:
        services = client.services
        if not services.get_service(SERVICE_UUID):
            raise RuntimeError("connected device does not expose the Passport plugin service")
        await synchronize(client, device_code)
        await client.write_gatt_char(CONTROL_UUID, struct.pack("<BI", 1, len(package)), response=True)
        status = await wait_for(client, {1, 6}, 15, "staging erase")
        if status.state == 6:
            raise RuntimeError(f"device rejected transfer start: ESP error {status.error}")

        mtu = getattr(client, "mtu_size", 23)
        payload_size = min(arguments.chunk_size, max(16, mtu - 7), 249)
        print(f"connected with MTU {mtu}; package chunk size {payload_size}")
        offset = 0
        while offset < len(package):
            chunk = package[offset:offset + payload_size]
            await client.write_gatt_char(
                DATA_UUID, struct.pack("<I", offset) + chunk, response=True
            )
            target = offset + len(chunk)
            while True:
                status = await read_status(client)
                if status.state == 6:
                    raise RuntimeError(f"device flash write failed: ESP error {status.error}")
                if status.received >= target:
                    break
                await asyncio.sleep(0.01)
            offset = target
            print(f"\ruploading: {offset}/{len(package)}", end="", flush=True)
        print()

        await client.write_gatt_char(CONTROL_UUID, b"\x02", response=True)
        status = await wait_for(client, {3, 6}, 30, "signature verification")
        if status.state == 6:
            raise RuntimeError(f"package verification failed: ESP error {status.error}")
        print("signature valid; review the plugin on the device and press OK to install")
        if arguments.no_wait_approval:
            return
        status = await wait_for(client, {5, 6}, arguments.approval_timeout, "device approval")
        if status.state == 6:
            raise RuntimeError(f"installation failed: ESP error {status.error}")
        print("plugin installed")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("package", type=Path)
    result.add_argument("--device-code", required=True, help="code shown by the Device/Nearby plugin")
    result.add_argument("--device", help="exact BLE name or platform address")
    result.add_argument("--chunk-size", type=int, default=240)
    result.add_argument("--scan-timeout", type=float, default=10)
    result.add_argument("--connect-timeout", type=float, default=20)
    result.add_argument("--approval-timeout", type=float, default=120)
    result.add_argument("--no-wait-approval", action="store_true")
    return result


def main() -> int:
    try:
        asyncio.run(upload(parser().parse_args()))
        return 0
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
