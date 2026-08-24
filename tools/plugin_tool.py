#!/usr/bin/env python3
"""Build, sign, and inspect FoloToy Passport plugin packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from ui_charset import describe_characters, unsupported_plugin_characters


PACKAGE_MAGIC = b"FPP1"
PACKAGE_VERSION = 1
PACKAGE_HEADER_SIZE = 108
MANIFEST_MAGIC = b"PLG5"
MANIFEST_VERSION = 5
MANIFEST_SIZE = 192
BYTECODE_VERSION = 1
HOST_API_VERSION = 5
MAX_PACKAGE_SIZE = 0x3F000
MANIFEST_HANDLERS_OFFSET = 36
MANIFEST_ID_OFFSET = 80
MANIFEST_NAME_OFFSET = 112
MANIFEST_AUTHOR_OFFSET = 160
MANIFEST_END_OFFSET = 192
MANIFEST_PREFIX_FORMAT = "<4sHHHBBIIIH2xII"

EVENTS = (
    "start", "up", "down", "ok", "timer0", "timer1", "timer2", "timer3",
    "back", "action", "nearby",
)
PLUGIN_KINDS = {"app": 0, "theme": 1}
PERMISSIONS = {
    "storage": 1 << 0,
    "audio": 1 << 1,
    "nearby": 1 << 2,
    "settings": 1 << 3,
    "microphone": 1 << 4,
}
PERMISSION_MASK = sum(PERMISSIONS.values())
OPERATION_PERMISSIONS = {
    "tone": ("audio",),
    "kv_load": ("storage",),
    "kv_save": ("storage",),
    "setting_load": ("settings",),
    "setting_save": ("settings",),
    "device_info": ("settings",),
    "theme_next": ("settings",),
    "nearby_acquire": ("nearby",),
    "nearby_release": ("nearby",),
    "nearby_send": ("nearby",),
    "nearby_blob_accept": ("nearby",),
    "nearby_blob_reject": ("nearby",),
    "nearby_blob_send": ("nearby",),
    "nearby_voice_start": ("nearby", "audio", "microphone"),
    "nearby_voice_transmit": ("nearby", "audio", "microphone"),
    "nearby_voice_stop": ("nearby", "audio", "microphone"),
}
SOURCE_FIELDS = {
    "id", "name", "author", "version", "host_api", "permissions",
    "state_slots", "templates", "handlers", "kind", "theme",
}
SYSTEM_SETTINGS_ID = "system.settings"
SYSTEM_NAMESPACE = "system."
ALIGNMENTS = {"left": 0, "center": 1, "right": 2}
EVENT_DATA_FIELDS = {"type": 0, "id": 1, "handle": 2, "value": 3}
UI_VALUE_KINDS = {
    "none": 0,
    "text": 1,
    "integer": 2,
    "percent": 3,
    "toggle": 4,
    "duration": 5,
    "theme": 6,
}
THEME_COLORS = {
    "background": 0,
    "surface": 1,
    "text": 2,
    "text_muted": 3,
    "accent": 4,
    "accent_strong": 5,
    "selection": 6,
    "muted_surface": 7,
    "danger": 8,
    "success": 9,
    "border": 10,
    "selection_border": 11,
}
THEME_DECORATIONS = {"none": 0, "pixel_ground": 1}
THEME_MAGIC = b"THM1"
THEME_VERSION = 1
THEME_SIZE = 64
THEME_COLOR_REFERENCE_FLAG = 0xFF000000
OPCODES = {
    "end": 0x00,
    "push": 0x01,
    "load_state": 0x02,
    "store_state": 0x03,
    "add": 0x04,
    "sub": 0x05,
    "mul": 0x06,
    "div": 0x07,
    "mod": 0x08,
    "eq": 0x09,
    "lt": 0x0A,
    "gt": 0x0B,
    "not": 0x0C,
    "dup": 0x0D,
    "drop": 0x0E,
    "jump": 0x10,
    "jz": 0x11,
    "jnz": 0x12,
    "event_load": 0x13,
    "ui_clear": 0x20,
    "ui_title": 0x21,
    "ui_text": 0x22,
    "ui_state": 0x23,
    "ui_rect": 0x24,
    "ui_commit": 0x25,
    "device_info": 0x26,
    "tone": 0x30,
    "kv_load": 0x31,
    "kv_save": 0x32,
    "timer_set": 0x33,
    "exit": 0x34,
    "setting_load": 0x35,
    "setting_save": 0x36,
    "ui_screen": 0x40,
    "ui_value_card": 0x41,
    "ui_list_row": 0x42,
    "ui_action_bar": 0x43,
    "ui_dialog_confirm": 0x44,
    "theme_next": 0x45,
    "theme_color": 0x46,
    "buffer_alloc": 0x50,
    "buffer_release": 0x51,
    "buffer_length": 0x52,
    "buffer_read_u8": 0x53,
    "buffer_write_u8": 0x54,
    "buffer_append_text": 0x55,
    "nearby_acquire": 0x58,
    "nearby_release": 0x59,
    "nearby_send": 0x5A,
    "nearby_blob_accept": 0x5B,
    "nearby_blob_reject": 0x5C,
    "nearby_blob_send": 0x5D,
    "nearby_voice_start": 0x5E,
    "nearby_voice_transmit": 0x5F,
    "nearby_voice_stop": 0x60,
}


class PluginToolError(ValueError):
    pass


def integer(value: Any, minimum: int, maximum: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise PluginToolError(f"{name} must be an integer in [{minimum}, {maximum}]")
    return value


def fixed_ascii(value: Any, size: int, name: str, identifier: bool = False) -> bytes:
    if not isinstance(value, str) or not value:
        raise PluginToolError(f"{name} must be a non-empty string")
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise PluginToolError(f"{name} must use printable ASCII") from exc
    if len(raw) >= size or any(byte < 0x20 or byte > 0x7E for byte in raw):
        raise PluginToolError(f"{name} must be printable ASCII shorter than {size} bytes")
    if identifier and any(not (chr(byte).isalnum() or chr(byte) in "_-." ) for byte in raw):
        raise PluginToolError(f"{name} may only contain ASCII letters, numbers, '_', '-', '.'")
    return raw + bytes(size - len(raw))


def fixed_utf8(value: Any, size: int, name: str) -> bytes:
    if not isinstance(value, str) or not value:
        raise PluginToolError(f"{name} must be a non-empty string")
    if any(ord(character) < 0x20 or 0x7F <= ord(character) <= 0x9F
           for character in value):
        raise PluginToolError(f"{name} cannot contain control characters")
    unsupported = unsupported_plugin_characters(value)
    if unsupported:
        raise PluginToolError(
            f"{name} uses characters outside the public 14px plugin font: "
            f"{describe_characters(unsupported)}"
        )
    try:
        raw = value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise PluginToolError(f"{name} must be valid UTF-8") from exc
    if len(raw) >= size:
        raise PluginToolError(f"{name} must be shorter than {size} UTF-8 bytes")
    return raw + bytes(size - len(raw))


def color(value: Any, *, allow_theme: bool = False) -> int:
    if isinstance(value, str) and value.startswith("theme:"):
        token = value.removeprefix("theme:")
        if not allow_theme:
            raise PluginToolError("theme package colors must be concrete #RRGGBB values")
        if token not in THEME_COLORS:
            raise PluginToolError(f"unknown theme color token: {token!r}")
        return THEME_COLOR_REFERENCE_FLAG | THEME_COLORS[token]
    if isinstance(value, str) and len(value) == 7 and value.startswith("#"):
        try:
            return int(value[1:], 16)
        except ValueError as exc:
            raise PluginToolError(f"invalid color: {value}") from exc
    return integer(value, 0, 0xFFFFFF, "color")


def bounded_utf8(value: Any, maximum: int, name: str) -> str:
    if not isinstance(value, str) or "\0" in value:
        raise PluginToolError(f"{name} must be a string without NUL characters")
    if len(value.encode("utf-8")) > maximum:
        raise PluginToolError(f"{name} exceeds {maximum} UTF-8 bytes")
    unsupported = unsupported_plugin_characters(value)
    if unsupported:
        raise PluginToolError(
            f"{name} uses characters outside the public 14px plugin font: "
            f"{describe_characters(unsupported)}"
        )
    return value


@dataclass
class StringTable:
    data: bytearray
    offsets: dict[str, int]

    @classmethod
    def create(cls) -> "StringTable":
        return cls(bytearray(), {})

    def add(self, value: Any) -> int:
        if not isinstance(value, str):
            raise PluginToolError("bytecode string operand must be a string")
        if "\0" in value:
            raise PluginToolError("bytecode strings cannot contain NUL characters")
        unsupported = unsupported_plugin_characters(value)
        if unsupported:
            raise PluginToolError(
                "bytecode string uses characters outside the public 14px plugin font: "
                f"{describe_characters(unsupported)}"
            )
        try:
            raw = value.encode("utf-8") + b"\0"
        except UnicodeEncodeError as exc:
            raise PluginToolError("invalid UTF-8 string") from exc
        if value in self.offsets:
            return self.offsets[value]
        if len(self.data) + len(raw) > 0xFFFF:
            raise PluginToolError("string table exceeds 65535 bytes")
        offset = len(self.data)
        self.data.extend(raw)
        self.offsets[value] = offset
        return offset


def require_args(instruction: list[Any], count: int) -> None:
    if len(instruction) != count + 1:
        raise PluginToolError(
            f"instruction '{instruction[0]}' expects {count} operand(s), got {len(instruction) - 1}"
        )


def expand_instructions(
    instructions: Any, templates: dict[str, Any], owner: str,
    trail: tuple[str, ...] = (),
) -> list[Any]:
    if not isinstance(instructions, list):
        raise PluginToolError(f"instruction block '{owner}' must be an array")
    expanded: list[Any] = []
    for instruction in instructions:
        if (isinstance(instruction, list) and instruction and
                instruction[0] == "include"):
            require_args(instruction, 1)
            template = instruction[1]
            if not isinstance(template, str) or template not in templates:
                raise PluginToolError(f"unknown instruction template: {template!r}")
            if template in trail:
                raise PluginToolError(f"recursive instruction template: {template}")
            expanded.extend(expand_instructions(
                templates[template], templates, template, trail + (template,)
            ))
        else:
            expanded.append(instruction)
    return expanded


def assemble_block(
    instructions: Any, strings: StringTable, event: str, state_slots: int
) -> bytes:
    if not isinstance(instructions, list):
        raise PluginToolError(f"handler '{event}' must be an array")
    code = bytearray()
    labels: dict[str, int] = {}
    fixups: list[tuple[int, str]] = []
    last_operation = ""

    def state_slot(value: Any) -> int:
        if state_slots == 0:
            raise PluginToolError(f"handler '{event}' uses state but state_slots is zero")
        return integer(value, 0, state_slots - 1, "state slot")

    def storage_key(value: Any) -> str:
        fixed_ascii(value, 16, "storage key")
        if any(ord(character) < 0x21 for character in value):
            raise PluginToolError("storage key cannot contain spaces or control characters")
        return value

    for index, raw_instruction in enumerate(instructions):
        if not isinstance(raw_instruction, list) or not raw_instruction or not isinstance(raw_instruction[0], str):
            raise PluginToolError(f"{event}[{index}] must be a non-empty instruction array")
        instruction = raw_instruction
        operation = instruction[0]
        if operation == "label":
            require_args(instruction, 1)
            label = instruction[1]
            if not isinstance(label, str) or not label or label in labels:
                raise PluginToolError(f"invalid or duplicate label in '{event}': {label!r}")
            labels[label] = len(code)
            continue
        if operation not in OPCODES:
            raise PluginToolError(f"unknown instruction in '{event}': {operation}")
        code.append(OPCODES[operation])
        last_operation = operation

        if operation == "push":
            require_args(instruction, 1)
            code.extend(struct.pack("<i", integer(instruction[1], -(1 << 31), (1 << 31) - 1, "value")))
        elif operation in ("load_state", "store_state"):
            require_args(instruction, 1)
            code.extend(struct.pack("<B", state_slot(instruction[1])))
        elif operation == "event_load":
            require_args(instruction, 2)
            field = instruction[1]
            if field not in EVENT_DATA_FIELDS:
                raise PluginToolError("event field must be type, id, handle, or value")
            code.extend(struct.pack(
                "<BB", EVENT_DATA_FIELDS[field], state_slot(instruction[2])
            ))
        elif operation in ("add", "sub", "mul", "div", "mod", "eq", "lt", "gt", "not", "dup", "drop", "ui_commit", "device_info", "nearby_acquire", "nearby_release", "nearby_voice_start", "nearby_voice_stop", "exit", "end"):
            require_args(instruction, 0)
        elif operation in ("jump", "jz", "jnz"):
            require_args(instruction, 1)
            if not isinstance(instruction[1], str):
                raise PluginToolError("jump target must be a label")
            fixups.append((len(code), instruction[1]))
            code.extend(b"\0\0")
        elif operation == "ui_clear":
            require_args(instruction, 1)
            code.extend(struct.pack("<I", color(instruction[1], allow_theme=True)))
        elif operation == "ui_title":
            require_args(instruction, 1)
            code.extend(struct.pack("<H", strings.add(bounded_utf8(instruction[1], 48, "title"))))
        elif operation in ("ui_text", "ui_state"):
            expected = 6 if operation == "ui_text" else 7
            require_args(instruction, expected)
            x = integer(instruction[1], -32768, 32767, "x")
            y = integer(instruction[2], -40, 320, "y")
            font = integer(instruction[3], 0, 0, "font")
            alignment = instruction[4]
            if alignment not in ALIGNMENTS:
                raise PluginToolError("alignment must be left, center, or right")
            if alignment == "left" and not -40 <= x <= 240:
                raise PluginToolError("left-aligned x must be in [-40, 240]")
            if alignment == "right" and not 0 <= x <= 280:
                raise PluginToolError("right-aligned x must be in [0, 280]")
            code.extend(struct.pack(
                "<hhBBIH", x, y, font, ALIGNMENTS[alignment],
                color(instruction[5], allow_theme=True),
                strings.add(bounded_utf8(
                    instruction[6], 128 if operation == "ui_text" else 64,
                    "text" if operation == "ui_text" else "state prefix"
                ))
            ))
            if operation == "ui_state":
                code.extend(struct.pack("<B", state_slot(instruction[7])))
        elif operation == "ui_rect":
            require_args(instruction, 5)
            values = [
                integer(instruction[1], -240, 32767, "x"),
                integer(instruction[2], -320, 32767, "y"),
                integer(instruction[3], 1, 480, "width"),
                integer(instruction[4], 1, 640, "height"),
            ]
            code.extend(struct.pack(
                "<hhhhI", *values, color(instruction[5], allow_theme=True)
            ))
        elif operation == "ui_screen":
            require_args(instruction, 1)
            code.extend(struct.pack(
                "<H", strings.add(bounded_utf8(instruction[1], 48, "screen title"))
            ))
        elif operation == "ui_value_card":
            require_args(instruction, 4)
            kind = instruction[2]
            if kind not in UI_VALUE_KINDS or kind in ("none", "text"):
                raise PluginToolError(
                    "value card format must be integer, percent, toggle, duration, or theme"
                )
            code.extend(struct.pack(
                "<HBBH",
                strings.add(bounded_utf8(instruction[1], 48, "value card label")),
                UI_VALUE_KINDS[kind], state_slot(instruction[3]),
                strings.add(bounded_utf8(instruction[4], 24, "value card suffix")),
            ))
        elif operation == "ui_list_row":
            require_args(instruction, 7)
            row_id = integer(instruction[1], 0, 7, "row id")
            icon = bounded_utf8(instruction[2], 16, "row icon")
            label = bounded_utf8(instruction[3], 48, "row label")
            kind = instruction[4]
            if kind not in UI_VALUE_KINDS:
                raise PluginToolError(
                    "row value format must be none, text, integer, percent, toggle, duration, or theme"
                )
            if kind in ("none", "text"):
                if not isinstance(instruction[5], str):
                    raise PluginToolError(f"{kind} row value must be a string")
                value_slot = 0xFF
                text_value = bounded_utf8(instruction[5], 48, "row text value")
            else:
                value_slot = state_slot(instruction[5])
                text_value = ""
            selected_slot = state_slot(instruction[6])
            enabled = instruction[7]
            if not isinstance(enabled, bool):
                raise PluginToolError("row enabled operand must be true or false")
            code.extend(struct.pack(
                "<BBBBBHHH", row_id, UI_VALUE_KINDS[kind], value_slot,
                selected_slot, enabled, strings.add(icon), strings.add(label),
                strings.add(text_value),
            ))
        elif operation == "ui_action_bar":
            require_args(instruction, 3)
            code.extend(struct.pack(
                "<HHH",
                strings.add(bounded_utf8(instruction[1], 24, "navigation action")),
                strings.add(bounded_utf8(instruction[2], 24, "OK action")),
                strings.add(bounded_utf8(instruction[3], 24, "back action")),
            ))
        elif operation == "ui_dialog_confirm":
            require_args(instruction, 5)
            code.extend(struct.pack(
                "<HHHHH", integer(instruction[1], 1, 0xFFFF, "dialog id"),
                strings.add(bounded_utf8(instruction[2], 48, "dialog title")),
                strings.add(bounded_utf8(instruction[3], 96, "dialog message")),
                strings.add(bounded_utf8(instruction[4], 24, "dialog cancel label")),
                strings.add(bounded_utf8(instruction[5], 24, "dialog confirm label")),
            ))
        elif operation == "theme_next":
            require_args(instruction, 1)
            code.extend(struct.pack("<B", state_slot(instruction[1])))
        elif operation == "theme_color":
            require_args(instruction, 2)
            token = instruction[1]
            if token not in THEME_COLORS:
                raise PluginToolError(f"unknown theme color token: {token!r}")
            code.extend(struct.pack(
                "<BB", THEME_COLORS[token], state_slot(instruction[2])
            ))
        elif operation == "tone":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<HH", integer(instruction[1], 20, 10000, "frequency"),
                integer(instruction[2], 1, 1000, "duration")
            ))
        elif operation == "kv_load":
            require_args(instruction, 3)
            code.extend(struct.pack(
                "<BHi", state_slot(instruction[1]),
                strings.add(storage_key(instruction[2])),
                integer(instruction[3], -(1 << 31), (1 << 31) - 1, "fallback")
            ))
        elif operation == "kv_save":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<BH", state_slot(instruction[1]), strings.add(storage_key(instruction[2]))
            ))
        elif operation == "timer_set":
            require_args(instruction, 3)
            repeat = instruction[3]
            if not isinstance(repeat, bool):
                raise PluginToolError("timer repeat operand must be true or false")
            code.extend(struct.pack(
                "<BIB", integer(instruction[1], 0, 3, "timer id"),
                integer(instruction[2], 100, 3_600_000, "timer delay"), repeat
            ))
        elif operation in ("setting_load", "setting_save"):
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<BB", integer(instruction[1], 0, 4, "setting id"),
                state_slot(instruction[2])
            ))
        elif operation == "buffer_alloc":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<HB", integer(instruction[1], 1, 4096, "buffer capacity"),
                state_slot(instruction[2])
            ))
        elif operation == "buffer_release":
            require_args(instruction, 1)
            code.extend(struct.pack("<B", state_slot(instruction[1])))
        elif operation == "buffer_length":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<BB", state_slot(instruction[1]), state_slot(instruction[2])
            ))
        elif operation in ("buffer_read_u8", "buffer_write_u8"):
            require_args(instruction, 3)
            code.extend(struct.pack(
                "<BBB", state_slot(instruction[1]), state_slot(instruction[2]),
                state_slot(instruction[3])
            ))
        elif operation == "buffer_append_text":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<BH", state_slot(instruction[1]),
                strings.add(bounded_utf8(instruction[2], 1024, "buffer text"))
            ))
        elif operation == "nearby_send":
            require_args(instruction, 2)
            code.extend(struct.pack(
                "<BB", state_slot(instruction[1]), state_slot(instruction[2])
            ))
        elif operation in ("nearby_blob_accept", "nearby_blob_reject"):
            require_args(instruction, 1)
            code.extend(struct.pack("<B", state_slot(instruction[1])))
        elif operation == "nearby_blob_send":
            require_args(instruction, 4)
            code.extend(struct.pack(
                "<BHHB", state_slot(instruction[1]),
                strings.add(bounded_utf8(instruction[2], 63, "blob name")),
                strings.add(bounded_utf8(instruction[3], 47, "blob MIME type")),
                state_slot(instruction[4])
            ))
        elif operation == "nearby_voice_transmit":
            require_args(instruction, 1)
            code.extend(struct.pack("<B", state_slot(instruction[1])))

    if last_operation != "end":
        code.append(OPCODES["end"])
    for operand_offset, label in fixups:
        if label not in labels:
            raise PluginToolError(f"unknown label in '{event}': {label}")
        relative = labels[label] - (operand_offset + 2)
        if not -32768 <= relative <= 32767:
            raise PluginToolError(f"jump to '{label}' is outside the 16-bit range")
        struct.pack_into("<h", code, operand_offset, relative)
    return bytes(code)


def require_current_host_api(source: dict[str, Any]) -> int:
    if "host_api" not in source:
        raise PluginToolError(f"host_api is required and must be {HOST_API_VERSION}")
    return integer(source["host_api"], HOST_API_VERSION, HOST_API_VERSION, "host_api")


def build_theme_payload(value: Any) -> bytes:
    if not isinstance(value, dict) or set(value) != {"colors", "panel", "decoration"}:
        raise PluginToolError("theme must contain exactly colors, panel, and decoration")
    colors = value["colors"]
    if not isinstance(colors, dict) or set(colors) != set(THEME_COLORS):
        raise PluginToolError(
            "theme colors must define exactly: " + ", ".join(THEME_COLORS)
        )
    panel = value["panel"]
    panel_fields = {"radius", "border_width", "shadow_width", "shadow_x", "shadow_y"}
    if not isinstance(panel, dict) or set(panel) != panel_fields:
        raise PluginToolError(
            "theme panel must define radius, border_width, shadow_width, shadow_x, shadow_y"
        )
    decoration = value["decoration"]
    if decoration not in THEME_DECORATIONS:
        raise PluginToolError("theme decoration must be none or pixel_ground")

    payload = bytearray(THEME_SIZE)
    struct.pack_into("<4sHH", payload, 0, THEME_MAGIC, THEME_VERSION, THEME_SIZE)
    for name, index in THEME_COLORS.items():
        struct.pack_into("<I", payload, 8 + index * 4, color(colors[name]))
    struct.pack_into(
        "<BBBbbB", payload, 56,
        integer(panel["radius"], 0, 16, "panel radius"),
        integer(panel["border_width"], 0, 6, "panel border width"),
        integer(panel["shadow_width"], 0, 12, "panel shadow width"),
        integer(panel["shadow_x"], -12, 12, "panel shadow x"),
        integer(panel["shadow_y"], -12, 12, "panel shadow y"),
        THEME_DECORATIONS[decoration],
    )
    return bytes(payload)


def build_manifest(
    source: dict[str, Any], *, kind: str, payload_schema: int,
    permissions: int, state_slots: int, code_size: int, strings_size: int,
    handlers: list[int],
) -> bytes:
    host_api = require_current_host_api(source)
    plugin_id = source.get("id")
    manifest = bytearray(MANIFEST_SIZE)
    struct.pack_into(
        MANIFEST_PREFIX_FORMAT, manifest, 0,
        MANIFEST_MAGIC, MANIFEST_VERSION, BYTECODE_VERSION, host_api,
        PLUGIN_KINDS[kind], payload_schema,
        integer(source.get("version"), 1, 0xFFFFFFFF, "version"),
        permissions, 0, state_slots, code_size, strings_size,
    )
    struct.pack_into(
        f"<{len(EVENTS)}I", manifest, MANIFEST_HANDLERS_OFFSET, *handlers
    )
    manifest[MANIFEST_ID_OFFSET:MANIFEST_NAME_OFFSET] = fixed_ascii(
        plugin_id, 32, "id", identifier=True
    )
    manifest[MANIFEST_NAME_OFFSET:MANIFEST_AUTHOR_OFFSET] = fixed_utf8(
        source.get("name"), 48, "name"
    )
    manifest[MANIFEST_AUTHOR_OFFSET:MANIFEST_END_OFFSET] = fixed_utf8(
        source.get("author"), 32, "author"
    )
    return bytes(manifest)


def build_theme_content(source: dict[str, Any]) -> bytes:
    disallowed = set(source) & {"permissions", "state_slots", "templates", "handlers"}
    if disallowed:
        raise PluginToolError("theme packages cannot define " + ", ".join(sorted(disallowed)))
    plugin_id = source.get("id")
    if not isinstance(plugin_id, str) or not plugin_id.startswith("theme."):
        raise PluginToolError("theme package id must start with 'theme.'")
    payload = build_theme_payload(source.get("theme"))
    manifest = build_manifest(
        source, kind="theme", payload_schema=THEME_VERSION, permissions=0,
        state_slots=0, code_size=len(payload), strings_size=0,
        handlers=[0xFFFFFFFF] * len(EVENTS),
    )
    return manifest + payload


def build_content(source: dict[str, Any]) -> bytes:
    unknown_fields = set(source) - SOURCE_FIELDS
    if unknown_fields:
        raise PluginToolError(
            f"unknown top-level field(s): {', '.join(sorted(unknown_fields))}"
        )
    kind = source.get("kind", "app")
    if kind not in PLUGIN_KINDS:
        raise PluginToolError("kind must be app or theme")
    if kind == "theme":
        return build_theme_content(source)
    if "theme" in source:
        raise PluginToolError("app packages cannot define theme data")
    handlers_source = source.get("handlers")
    if not isinstance(handlers_source, dict):
        raise PluginToolError("handlers must be an object")
    unknown_events = set(handlers_source) - set(EVENTS)
    if unknown_events:
        raise PluginToolError(f"unknown event(s): {', '.join(sorted(unknown_events))}")
    templates = source.get("templates", {})
    if not isinstance(templates, dict) or any(
        not isinstance(name, str) or not name for name in templates
    ):
        raise PluginToolError("templates must be an object with named instruction arrays")
    expanded_handlers = {
        event: expand_instructions(block, templates, event)
        for event, block in handlers_source.items()
    }

    used_operations = {
        instruction[0]
        for block in expanded_handlers.values()
        for instruction in block
        if isinstance(instruction, list) and instruction and
        isinstance(instruction[0], str)
    }
    state_slots = integer(source.get("state_slots", 0), 0, 16, "state_slots")
    strings = StringTable.create()
    code = bytearray()
    handlers: list[int] = []
    for event in EVENTS:
        if event not in handlers_source:
            handlers.append(0xFFFFFFFF)
            continue
        handlers.append(len(code))
        code.extend(assemble_block(expanded_handlers[event], strings, event, state_slots))
    if not code:
        raise PluginToolError("at least one event handler is required")

    permission_names = source.get("permissions", [])
    if not isinstance(permission_names, list) or any(name not in PERMISSIONS for name in permission_names):
        raise PluginToolError(
            "permissions must contain only storage, audio, nearby, settings, or microphone"
        )
    for operation, required_permissions in OPERATION_PERMISSIONS.items():
        missing = [name for name in required_permissions if name not in permission_names]
        if operation in used_operations and missing:
            if len(required_permissions) == 1:
                raise PluginToolError(
                    f"{operation} requires the {required_permissions[0]} permission"
                )
            raise PluginToolError(
                f"{operation} requires permissions: {', '.join(required_permissions)}"
            )
    plugin_id = source.get("id")
    if isinstance(plugin_id, str) and plugin_id.startswith(SYSTEM_NAMESPACE):
        if plugin_id != SYSTEM_SETTINGS_ID:
            raise PluginToolError(
                f"{plugin_id} is reserved by the firmware and cannot be packaged"
            )
        if ("settings" not in permission_names or
                "device_info" not in used_operations or
                "theme_next" not in used_operations):
            raise PluginToolError(
                "system.settings requires the settings permission, "
                "and device_info and theme_next operations"
            )
    if isinstance(plugin_id, str) and plugin_id.startswith("theme."):
        raise PluginToolError("app package ids cannot use the reserved 'theme.' namespace")
    permissions = 0
    for name in permission_names:
        permissions |= PERMISSIONS[name]

    manifest = build_manifest(
        source, kind="app", payload_schema=0, permissions=permissions,
        state_slots=state_slots, code_size=len(code), strings_size=len(strings.data),
        handlers=handlers,
    )
    return manifest + bytes(code) + bytes(strings.data)


def load_private_key(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise PluginToolError("private key must be ECDSA P-256 (secp256r1)")
    return key


def load_public_key(path: Path) -> ec.EllipticCurvePublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise PluginToolError("public key must be ECDSA P-256 (secp256r1)")
    return key


def header_source(public_key: ec.EllipticCurvePublicKey) -> str:
    raw = public_key.public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
    )
    rows = []
    for offset in range(0, len(raw), 8):
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in raw[offset:offset + 8]) + ",")
    return (
        "#pragma once\n\n#include <stdint.h>\n\n"
        "/* Generated by tools/plugin_tool.py keygen. Keep the matching private key offline. */\n"
        "static const uint8_t PLUGIN_TRUSTED_PUBLIC_KEY[65] = {\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def command_keygen(arguments: argparse.Namespace) -> None:
    if arguments.private.exists() and not arguments.force:
        raise PluginToolError(f"refusing to overwrite existing key: {arguments.private}")
    private_key = ec.generate_private_key(ec.SECP256R1())
    arguments.private.parent.mkdir(parents=True, exist_ok=True)
    arguments.private.write_bytes(private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))
    os.chmod(arguments.private, 0o600)
    if arguments.public:
        arguments.public.parent.mkdir(parents=True, exist_ok=True)
        arguments.public.write_bytes(private_key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
        ))
    arguments.header.parent.mkdir(parents=True, exist_ok=True)
    arguments.header.write_text(header_source(private_key.public_key()), encoding="utf-8")
    print(f"created private signing key: {arguments.private}")
    print(f"embedded trust key header: {arguments.header}")


def command_pack(arguments: argparse.Namespace) -> None:
    source = json.loads(arguments.source.read_text(encoding="utf-8"))
    if not isinstance(source, dict):
        raise PluginToolError("plugin source must be a JSON object")
    content = build_content(source)
    prefix = struct.pack("<4sHHI", PACKAGE_MAGIC, PACKAGE_VERSION, PACKAGE_HEADER_SIZE, len(content))
    digest = hashlib.sha256(prefix + content).digest()
    private_key = load_private_key(arguments.private)
    signature_der = private_key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(signature_der)
    package = prefix + digest + r.to_bytes(32, "big") + s.to_bytes(32, "big") + content
    if len(package) > MAX_PACKAGE_SIZE:
        raise PluginToolError(f"package is {len(package)} bytes; limit is {MAX_PACKAGE_SIZE}")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(package)
    print(f"packed {source['id']} v{source['version']}: {arguments.output} ({len(package)} bytes)")


def parse_package(path: Path) -> tuple[bytes, bytes, bytes, dict[str, Any]]:
    package = path.read_bytes()
    if len(package) < PACKAGE_HEADER_SIZE:
        raise PluginToolError("package is truncated")
    magic, version, header_size, content_size = struct.unpack_from("<4sHHI", package)
    if magic != PACKAGE_MAGIC or version != PACKAGE_VERSION or header_size != PACKAGE_HEADER_SIZE:
        raise PluginToolError("unsupported package header")
    if header_size + content_size != len(package):
        raise PluginToolError("package length does not match its header")
    digest = package[12:44]
    signature = package[44:108]
    content = package[header_size:]
    calculated = hashlib.sha256(package[:12] + content).digest()
    if calculated != digest:
        raise PluginToolError("package digest is invalid")
    if content[:4] != MANIFEST_MAGIC or len(content) < MANIFEST_SIZE:
        raise PluginToolError("manifest is invalid")
    fields = struct.unpack_from(MANIFEST_PREFIX_FORMAT, content)
    if fields[1:4] != (MANIFEST_VERSION, BYTECODE_VERSION, HOST_API_VERSION):
        raise PluginToolError("unsupported manifest, bytecode, or host API version")
    kind, payload_schema = fields[4:6]
    if (kind not in PLUGIN_KINDS.values() or fields[6] == 0 or
            fields[7] & ~PERMISSION_MASK or fields[8] != 0 or
            fields[9] > 16 or fields[10] == 0 or content[26:28] != bytes(2)):
        raise PluginToolError("manifest layout is invalid")
    if ((kind == PLUGIN_KINDS["app"] and payload_schema != 0) or
            (kind == PLUGIN_KINDS["theme"] and
             (payload_schema != THEME_VERSION or fields[7] != 0 or fields[9] != 0))):
        raise PluginToolError("manifest kind or payload schema is invalid")
    try:
        plugin_id = content[MANIFEST_ID_OFFSET:MANIFEST_NAME_OFFSET].split(
            b"\0", 1
        )[0].decode("ascii")
        plugin_name = content[MANIFEST_NAME_OFFSET:MANIFEST_AUTHOR_OFFSET].split(
            b"\0", 1
        )[0].decode("utf-8")
        plugin_author = content[MANIFEST_AUTHOR_OFFSET:MANIFEST_END_OFFSET].split(
            b"\0", 1
        )[0].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PluginToolError("manifest text encoding is invalid") from exc
    if (not plugin_id or not plugin_name or not plugin_author or
            any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789_-." for character in plugin_id)):
        raise PluginToolError("manifest text is invalid")
    metadata = {
        "id": plugin_id,
        "name": plugin_name,
        "author": plugin_author,
        "version": fields[6],
        "permissions": fields[7],
        "state_slots": fields[9],
        "code_size": fields[10],
        "strings_size": fields[11],
        "package_size": len(package),
        "manifest_version": fields[1],
        "host_api": fields[3],
        "kind": "theme" if kind == PLUGIN_KINDS["theme"] else "app",
        "payload_schema": payload_schema,
    }
    if MANIFEST_SIZE + metadata["code_size"] + metadata["strings_size"] != len(content):
        raise PluginToolError("manifest content sizes are invalid")
    if metadata["kind"] == "theme" and (
        metadata["code_size"] != THEME_SIZE or metadata["strings_size"] != 0 or
        content[MANIFEST_SIZE:MANIFEST_SIZE + 4] != THEME_MAGIC
    ):
        raise PluginToolError("theme payload is invalid")
    return digest, signature, content, metadata


def command_inspect(arguments: argparse.Namespace) -> None:
    digest, signature, _, metadata = parse_package(arguments.package)
    verified = None
    if arguments.public:
        r = int.from_bytes(signature[:32], "big")
        s = int.from_bytes(signature[32:], "big")
        der = utils.encode_dss_signature(r, s)
        try:
            load_public_key(arguments.public).verify(
                der, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256()))
            )
            verified = True
        except InvalidSignature:
            verified = False
    metadata["digest_sha256"] = digest.hex()
    if verified is not None:
        metadata["signature_valid"] = verified
    print(json.dumps(metadata, indent=2, ensure_ascii=False))
    if verified is False:
        raise PluginToolError("signature does not match the supplied public key")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    keygen = subparsers.add_parser("keygen", help="generate a P-256 signing and trust key")
    keygen.add_argument("--private", type=Path, required=True)
    keygen.add_argument("--public", type=Path)
    keygen.add_argument("--header", type=Path, required=True)
    keygen.add_argument("--force", action="store_true")
    keygen.set_defaults(function=command_keygen)

    pack = subparsers.add_parser("pack", help="compile and sign a JSON plugin")
    pack.add_argument("source", type=Path)
    pack.add_argument("--private", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    pack.set_defaults(function=command_pack)

    inspect = subparsers.add_parser("inspect", help="inspect and optionally verify a package")
    inspect.add_argument("package", type=Path)
    inspect.add_argument("--public", type=Path)
    inspect.set_defaults(function=command_inspect)
    return result


def main() -> int:
    try:
        arguments = parser().parse_args()
        arguments.function(arguments)
        return 0
    except (OSError, json.JSONDecodeError, PluginToolError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
