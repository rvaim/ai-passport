#!/usr/bin/env python3
"""Reference client for the Passport foreground Nearby Runtime Gateway."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import mimetypes
import re
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "f0772000-6f6c-6f74-6f79-70617373706f"
CONTROL_UUID = "f0772001-6f6c-6f74-6f79-70617373706f"
RX_UUID = "f0772002-6f6c-6f74-6f79-70617373706f"
TX_UUID = "f0772003-6f6c-6f74-6f79-70617373706f"
STATUS_UUID = "f0772004-6f6c-6f74-6f79-70617373706f"

PROTOCOL_VERSION = 1
HEADER_SIZE = 16
PAYLOAD_MAX = 237
FIRST = 1 << 0
LAST = 1 << 1
ACCEPT = 1 << 2

MESSAGE = 1
BLOB_OFFER = 2
BLOB_DECISION = 3
BLOB_DATA = 4
BLOB_COMPLETE = 5
BLOB_CANCEL = 6
VOICE = 7
ACK = 8
ERROR = 9


@dataclass(frozen=True)
class Frame:
    type: int
    flags: int
    id: int
    offset: int
    total: int
    payload: bytes


def normalize_device_code(value: str) -> str:
    compact = re.sub(r"[-\s]", "", value.upper())
    if not re.fullmatch(r"[0-9A-HJKMNP-TV-Z]{10}", compact):
        raise ValueError("device code must be the ten characters shown on the device")
    return compact


def encode_frame(
    frame_type: int, flags: int, identifier: int, offset: int,
    total: int, payload: bytes = b"",
) -> bytes:
    if not 1 <= frame_type <= ERROR or len(payload) > PAYLOAD_MAX:
        raise ValueError("invalid Nearby frame")
    return struct.pack(
        "<BBBBIII", PROTOCOL_VERSION, frame_type, flags, 0,
        identifier, offset, total,
    ) + payload


def decode_frame(raw: bytes) -> Frame:
    if not HEADER_SIZE <= len(raw) <= HEADER_SIZE + PAYLOAD_MAX:
        raise ValueError("invalid Nearby frame length")
    version, frame_type, flags, reserved, identifier, offset, total = struct.unpack_from(
        "<BBBBIII", raw
    )
    if version != PROTOCOL_VERSION or reserved != 0 or not 1 <= frame_type <= ERROR:
        raise ValueError("invalid Nearby frame header")
    return Frame(frame_type, flags, identifier, offset, total, raw[HEADER_SIZE:])


async def find_device(selector: str | None, timeout: float):
    print("scanning for Passport Runtime Gateway...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for device, advertisement in devices.values():
        name = advertisement.local_name or device.name or ""
        advertised = {value.lower() for value in advertisement.service_uuids}
        if selector and selector not in (device.address, name):
            continue
        if SERVICE_UUID in advertised:
            print(f"found {name or '(unnamed)'} [{device.address}]")
            return device
    target = f" matching {selector!r}" if selector else ""
    raise RuntimeError(
        f"no Passport Runtime Gateway{target} found; open a plugin that acquired Nearby"
    )


async def read_status(client: BleakClient) -> tuple[int, bool, int]:
    raw = bytes(await client.read_gatt_char(STATUS_UUID))
    if len(raw) != 8 or raw[0] != 1 or raw[3] != 0:
        raise RuntimeError(f"unsupported Runtime status: {raw.hex()}")
    return raw[1], raw[2] != 0, struct.unpack_from("<I", raw, 4)[0]


async def synchronize(client: BleakClient, compact_code: str) -> None:
    await client.write_gatt_char(
        CONTROL_UUID, b"\x10" + compact_code.encode("ascii"), response=True
    )
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        state, subscribed, generation = await read_status(client)
        if state == 2:
            if not subscribed:
                raise RuntimeError("TX notifications are not subscribed")
            print(f"device code matched; session {generation} synchronized")
            return
        if state == 3:
            raise RuntimeError("device code does not match this Passport")
        await asyncio.sleep(0.05)
    raise TimeoutError("timed out waiting for device-code synchronization")


async def write_payload(
    client: BleakClient, frame_type: int, identifier: int, payload: bytes,
) -> None:
    if not payload:
        await client.write_gatt_char(
            RX_UUID,
            encode_frame(frame_type, FIRST | LAST, identifier, 0, 0),
            response=True,
        )
        return
    offset = 0
    while offset < len(payload):
        chunk = payload[offset:offset + PAYLOAD_MAX]
        flags = (FIRST if offset == 0 else 0) | (
            LAST if offset + len(chunk) == len(payload) else 0
        )
        await client.write_gatt_char(
            RX_UUID,
            encode_frame(frame_type, flags, identifier, offset, len(payload), chunk),
            response=True,
        )
        offset += len(chunk)


class RuntimeSession:
    def __init__(self, client: BleakClient, output_directory: Path | None):
        self.client = client
        self.output_directory = output_directory
        self.frames: asyncio.Queue[Frame] = asyncio.Queue()
        self.message_id: int | None = None
        self.message = bytearray()
        self.incoming_blob: tuple[int, str, str, bytes, bytearray] | None = None

    def notification(self, _characteristic, raw: bytearray) -> None:
        try:
            self.frames.put_nowait(decode_frame(bytes(raw)))
        except (ValueError, asyncio.QueueFull) as error:
            print(f"discarded notification: {error}", file=sys.stderr)

    async def process(self, frame: Frame) -> None:
        if frame.type == MESSAGE:
            if frame.flags & FIRST:
                self.message_id = frame.id
                self.message = bytearray()
            if self.message_id != frame.id or frame.offset != len(self.message):
                raise RuntimeError("out-of-order message from device")
            self.message.extend(frame.payload)
            if frame.flags & LAST:
                if len(self.message) != frame.total:
                    raise RuntimeError("message total does not match")
                print(f"message {frame.id}: {self.message.decode('utf-8', errors='replace')}")
                self.message_id = None
            return
        if frame.type == BLOB_OFFER:
            await self._accept_device_blob(frame)
            return
        if frame.type == BLOB_DATA:
            self._append_device_blob(frame)
            return
        if frame.type == BLOB_COMPLETE:
            await self._finish_device_blob(frame)
            return
        if frame.type == VOICE:
            print(f"voice frame seq={frame.offset}, {len(frame.payload)} bytes")
            return
        if frame.type == ERROR:
            print(f"device protocol error id={frame.id} code={frame.total}", file=sys.stderr)

    async def _accept_device_blob(self, frame: Frame) -> None:
        if (
            frame.flags != (FIRST | LAST)
            or frame.offset != 0
            or len(frame.payload) < 34
        ):
            raise RuntimeError("truncated Blob offer")
        name_size, mime_size = frame.payload[32], frame.payload[33]
        if len(frame.payload) != 34 + name_size + mime_size:
            raise RuntimeError("invalid Blob offer metadata")
        name = frame.payload[34:34 + name_size].decode("utf-8")
        mime = frame.payload[34 + name_size:].decode("utf-8")
        if self.output_directory is None:
            await self.client.write_gatt_char(
                RX_UUID, encode_frame(BLOB_DECISION, 0, frame.id, 0, 0), response=True
            )
            print(f"rejected Blob {frame.id}: {name} ({mime}, {frame.total} bytes)")
            return
        self.incoming_blob = (frame.id, name, mime, frame.payload[:32], bytearray())
        await self.client.write_gatt_char(
            RX_UUID, encode_frame(BLOB_DECISION, ACCEPT, frame.id, 0, 0), response=True
        )
        print(f"accepted Blob {frame.id}: {name} ({mime}, {frame.total} bytes)")

    def _append_device_blob(self, frame: Frame) -> None:
        if self.incoming_blob is None or self.incoming_blob[0] != frame.id:
            raise RuntimeError("Blob data without accepted offer")
        data = self.incoming_blob[4]
        if frame.offset != len(data) or frame.total < frame.offset + len(frame.payload):
            raise RuntimeError("out-of-order Blob data")
        data.extend(frame.payload)

    async def _finish_device_blob(self, frame: Frame) -> None:
        if self.incoming_blob is None or self.incoming_blob[0] != frame.id:
            raise RuntimeError("Blob completion without accepted offer")
        identifier, name, _mime, expected_digest, data = self.incoming_blob
        actual_digest = hashlib.sha256(data).digest()
        if (
            frame.flags != LAST
            or frame.offset != frame.total
            or len(data) != frame.total
            or frame.payload != expected_digest
            or actual_digest != expected_digest
        ):
            self.incoming_blob = None
            raise RuntimeError("received Blob digest does not match")
        assert self.output_directory is not None
        self.output_directory.mkdir(parents=True, exist_ok=True)
        safe_name = Path(name).name or f"blob-{identifier}.bin"
        output = self.output_directory / safe_name
        output.write_bytes(data)
        await self.client.write_gatt_char(
            RX_UUID,
            encode_frame(ACK, ACCEPT, identifier, len(data), len(data), expected_digest),
            response=True,
        )
        self.incoming_blob = None
        print(f"saved Blob {identifier}: {output}")


async def wait_for_frame(
    session: RuntimeSession, predicate, timeout: float,
) -> Frame:
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("timed out waiting for the device")
        frame = await asyncio.wait_for(session.frames.get(), remaining)
        if predicate(frame):
            return frame
        await session.process(frame)


async def send_file(
    client: BleakClient, session: RuntimeSession, path: Path, timeout: float,
) -> None:
    data = path.read_bytes()
    if not data or len(data) > 0xC0000:
        raise ValueError("Blob must contain 1..786432 bytes")
    name = path.name.encode("utf-8")
    mime = (mimetypes.guess_type(path.name)[0] or "application/octet-stream").encode("utf-8")
    if not 1 <= len(name) <= 63 or not 1 <= len(mime) <= 47:
        raise ValueError("Blob name or MIME type exceeds the firmware limit")
    identifier = int(time.time_ns() & 0x7FFFFFFF) or 1
    digest = hashlib.sha256(data).digest()
    offer = digest + bytes((len(name), len(mime))) + name + mime
    await client.write_gatt_char(
        RX_UUID, encode_frame(BLOB_OFFER, FIRST | LAST, identifier, 0, len(data), offer),
        response=True,
    )
    decision = await wait_for_frame(
        session, lambda frame: frame.type == BLOB_DECISION and frame.id == identifier,
        timeout,
    )
    if not decision.flags & ACCEPT:
        raise RuntimeError("plugin rejected the Blob")
    print(f"Blob {identifier} accepted; sending {len(data)} bytes")
    await write_payload(client, BLOB_DATA, identifier, data)
    await client.write_gatt_char(
        RX_UUID,
        encode_frame(BLOB_COMPLETE, LAST, identifier, len(data), len(data), digest),
        response=True,
    )
    await wait_for_frame(
        session, lambda frame: frame.type == ACK and frame.id == identifier, timeout
    )
    print("Blob verified by device")


async def run(arguments: argparse.Namespace) -> None:
    device = await find_device(arguments.device, arguments.scan_timeout)
    compact_code = normalize_device_code(arguments.device_code)
    async with BleakClient(device, timeout=arguments.connect_timeout) as client:
        if not client.services.get_service(SERVICE_UUID):
            raise RuntimeError("connected device does not expose the Runtime Gateway")
        session = RuntimeSession(client, arguments.output_directory)
        await client.start_notify(TX_UUID, session.notification)
        await synchronize(client, compact_code)

        if arguments.command == "message":
            payload = arguments.text.encode("utf-8")
            if len(payload) > 4096:
                raise ValueError("message exceeds 4096 UTF-8 bytes")
            identifier = int(time.time_ns() & 0x7FFFFFFF) or 1
            await write_payload(client, MESSAGE, identifier, payload)
            print(f"sent message {identifier}: {len(payload)} bytes")
        elif arguments.command == "send-file":
            await send_file(client, session, arguments.path, arguments.timeout)

        deadline = None if arguments.listen == 0 else time.monotonic() + arguments.listen
        while deadline is None or time.monotonic() < deadline:
            timeout = None if deadline is None else max(0.01, deadline - time.monotonic())
            try:
                frame = await asyncio.wait_for(session.frames.get(), timeout)
            except asyncio.TimeoutError:
                break
            await session.process(frame)


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--device-code", required=True, help="code shown by the Passport")
    result.add_argument("--device", help="exact BLE name or platform address")
    result.add_argument("--scan-timeout", type=float, default=10)
    result.add_argument("--connect-timeout", type=float, default=20)
    result.add_argument("--timeout", type=float, default=30)
    result.add_argument(
        "--listen", type=float, default=3,
        help="seconds to process incoming notifications after the command; 0 waits forever",
    )
    result.add_argument(
        "--output-directory", type=Path,
        help="accept device-sent Blobs into this directory; otherwise reject them",
    )
    subparsers = result.add_subparsers(dest="command", required=True)
    message = subparsers.add_parser("message", help="send one UTF-8 message")
    message.add_argument("text")
    send_blob = subparsers.add_parser("send-file", help="offer and send one file")
    send_blob.add_argument("path", type=Path)
    subparsers.add_parser("listen", help="only receive notifications")
    return result


def main() -> int:
    try:
        asyncio.run(run(parser().parse_args()))
        return 0
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
