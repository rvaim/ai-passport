#!/usr/bin/env python3
"""Single source of truth for text accepted by installable plugins."""

from __future__ import annotations

import re
from functools import lru_cache
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
SYMBOL_HEADER = Path("managed_components/lvgl__lvgl/src/font/lv_symbol_def.h")
LV_SYMBOL_DEFINE_RE = re.compile(
    r'^\s*#define\s+(LV_SYMBOL_[A-Z0-9_]+)\s+"((?:\\x[0-9A-Fa-f]{2})+)"',
    re.MULTILINE,
)


def is_private_use(codepoint: int) -> bool:
    return (
        0xE000 <= codepoint <= 0xF8FF
        or 0xF0000 <= codepoint <= 0xFFFFD
        or 0x100000 <= codepoint <= 0x10FFFD
    )


@lru_cache(maxsize=1)
def gb2312_text_glyphs() -> frozenset[str]:
    characters: set[str] = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                value = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            characters.update(
                character
                for character in value
                if ord(character) > 0x7F
                and character.isprintable()
                and not character.isspace()
            )
    return frozenset(characters)


@lru_cache(maxsize=1)
def load_lv_symbols() -> dict[str, str]:
    source = (PROJECT_DIR / SYMBOL_HEADER).read_text(encoding="utf-8")
    symbols: dict[str, str] = {}
    for name, escaped_bytes in LV_SYMBOL_DEFINE_RE.findall(source):
        raw = bytes(
            int(hex_byte, 16)
            for hex_byte in re.findall(r"\\x([0-9A-Fa-f]{2})", escaped_bytes)
        )
        symbols[name] = raw.decode("utf-8")
    return symbols


@lru_cache(maxsize=1)
def plugin_icon_codepoints() -> frozenset[int]:
    return frozenset(
        ord(character)
        for name, value in load_lv_symbols().items()
        if name != "LV_SYMBOL_DUMMY"
        for character in value
        if is_private_use(ord(character))
    )


def unsupported_plugin_characters(value: str) -> set[str]:
    text_glyphs = gb2312_text_glyphs()
    icon_codepoints = plugin_icon_codepoints()
    unsupported: set[str] = set()
    for character in value:
        codepoint = ord(character)
        if character == "\n" or 0x20 <= codepoint <= 0x7E:
            continue
        if character in text_glyphs or codepoint in icon_codepoints:
            continue
        unsupported.add(character)
    return unsupported


def describe_characters(characters: set[str]) -> str:
    return " ".join(
        f"{character} (U+{ord(character):04X})"
        for character in sorted(characters, key=ord)
    )
