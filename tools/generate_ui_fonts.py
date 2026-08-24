#!/usr/bin/env python3
"""Generate and verify fonts from the firmware's real CMake source graph.

The 14 px font is the stable public plugin font.  The 18 px font is trimmed to
text used by the built-in firmware.  Reading every file under ``main`` made
removed prototypes and abandoned demos silently enlarge the firmware, so the
CMake ``SRCS`` list and its local header includes are the only built-in inputs.
"""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable

from ui_charset import (
    gb2312_text_glyphs,
    is_private_use,
    load_lv_symbols,
    plugin_icon_codepoints,
)


PROJECT_DIR = Path(__file__).resolve().parents[1]
TEXT_FONT = Path(
    "managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/"
    "NotoSansSC-Regular.ttf"
)
ICON_FONT = Path(
    "managed_components/lvgl__lvgl/scripts/built_in_font/"
    "FontAwesome5-Solid+Brands+Regular.woff"
)
FONT_OUTPUTS = {
    14: Path("main/ui_font_zh_14.c"),
    18: Path("main/ui_font_zh_18.c"),
}

C_STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"', re.DOTALL)
LOCAL_INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
LV_SYMBOL_TOKEN_RE = re.compile(r"\bLV_SYMBOL_[A-Z0-9_]+\b")
OPTS_RE = re.compile(r"^ \* Opts: (.+)$", re.MULTILINE)
CHARSET_OUTPUT = Path("components/plugin_runtime/src/plugin_charset_data.inc")
MAIN_DIR = PROJECT_DIR / "main"
MAIN_CMAKE = MAIN_DIR / "CMakeLists.txt"
GENERATED_FONT_PREFIX = "ui_font_zh_"


def compiled_source_files() -> list[Path]:
    """Return literal C/H entries from main's idf_component_register SRCS."""
    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    match = re.search(r"\bSRCS\b(.*?)\bINCLUDE_DIRS\b", cmake, re.DOTALL)
    if match is None:
        raise ValueError("main/CMakeLists.txt has no SRCS ... INCLUDE_DIRS block")

    files: list[Path] = []
    for relative in re.findall(r'"([^"]+)"', match.group(1)):
        path = MAIN_DIR / relative
        if path.suffix not in {".c", ".h"} or path.name.startswith(GENERATED_FONT_PREFIX):
            continue
        if not path.is_file():
            raise FileNotFoundError(path.relative_to(PROJECT_DIR))
        files.append(path)
    if not files:
        raise ValueError("main/CMakeLists.txt has no scannable source files")
    return files


def source_files() -> list[Path]:
    """Return compiled main sources plus local headers reachable from them."""
    pending = compiled_source_files()
    discovered: set[Path] = set()
    main_root = MAIN_DIR.resolve()

    while pending:
        path = pending.pop().resolve()
        if path in discovered:
            continue
        discovered.add(path)
        source = path.read_text(encoding="utf-8")
        for include in LOCAL_INCLUDE_RE.findall(source):
            candidates = (path.parent / include, MAIN_DIR / include)
            for candidate in candidates:
                candidate = candidate.resolve()
                try:
                    candidate.relative_to(main_root)
                except ValueError:
                    continue
                if (candidate.is_file() and candidate.suffix == ".h" and
                        not candidate.name.startswith(GENERATED_FONT_PREFIX)):
                    pending.append(candidate)
                    break
    return sorted(discovered)


def decode_c_literal(literal: str, path: Path) -> str:
    try:
        value = ast.literal_eval(literal)
    except (SyntaxError, ValueError) as exc:
        relative = path.relative_to(PROJECT_DIR)
        raise ValueError(f"cannot decode string literal in {relative}: {literal}") from exc
    if not isinstance(value, str):
        raise TypeError(f"unexpected non-string literal in {path}")
    return value


def strings_in_file(path: Path) -> Iterable[str]:
    source = path.read_text(encoding="utf-8")
    for match in C_STRING_RE.finditer(source):
        yield decode_c_literal(match.group(0), path)


def is_text_glyph(character: str) -> bool:
    codepoint = ord(character)
    return (
        codepoint > 0x7F
        and not is_private_use(codepoint)
        and character.isprintable()
        and not character.isspace()
    )


def collect_requirements() -> tuple[set[str], set[int], dict[str, set[str]]]:
    text_glyphs: set[str] = set()
    icon_codepoints: set[int] = set()
    origins: dict[str, set[str]] = defaultdict(set)
    lv_symbols = load_lv_symbols()

    for path in source_files():
        relative = str(path.relative_to(PROJECT_DIR))
        raw_source = path.read_text(encoding="utf-8")

        for token in LV_SYMBOL_TOKEN_RE.findall(raw_source):
            if token not in lv_symbols:
                raise ValueError(f"unknown LVGL symbol {token} in {relative}")
            for character in lv_symbols[token]:
                codepoint = ord(character)
                if is_private_use(codepoint):
                    icon_codepoints.add(codepoint)
                    origins[character].add(relative)

        for value in strings_in_file(path):
            for character in value:
                codepoint = ord(character)
                if is_private_use(codepoint):
                    icon_codepoints.add(codepoint)
                    origins[character].add(relative)
                elif is_text_glyph(character):
                    text_glyphs.add(character)
                    origins[character].add(relative)

    return text_glyphs, icon_codepoints, origins


def format_ranges(codepoints: Iterable[int]) -> str:
    ordered = sorted(set(codepoints))
    if not ordered:
        return ""

    ranges: list[str] = []
    start = previous = ordered[0]
    for codepoint in ordered[1:]:
        if codepoint == previous + 1:
            previous = codepoint
            continue
        ranges.append(
            f"0x{start:X}" if start == previous else f"0x{start:X}-0x{previous:X}"
        )
        start = previous = codepoint
    ranges.append(
        f"0x{start:X}" if start == previous else f"0x{start:X}-0x{previous:X}"
    )
    return ",".join(ranges)


def expand_ranges(value: str) -> set[int]:
    codepoints: set[int] = set()
    for part in value.split(","):
        if not part:
            continue
        if "-" in part:
            start_text, end_text = part.split("-", 1)
            codepoints.update(range(int(start_text, 0), int(end_text, 0) + 1))
        else:
            codepoints.add(int(part, 0))
    return codepoints


def generated_options(path: Path) -> tuple[set[str], set[int]]:
    source = path.read_text(encoding="utf-8")
    match = OPTS_RE.search(source)
    if match is None:
        raise ValueError(f"missing lv_font_conv options in {path.relative_to(PROJECT_DIR)}")

    options = match.group(1)
    symbols_match = re.search(r"(?:^|\s)--symbols\s+(\S+)", options)
    if symbols_match is None:
        raise ValueError(f"missing --symbols in {path.relative_to(PROJECT_DIR)}")

    text_glyphs = set(symbols_match.group(1))
    icon_codepoints: set[int] = set()
    for range_value in re.findall(r"(?:^|\s)-r\s+(\S+)", options):
        icon_codepoints.update(
            codepoint
            for codepoint in expand_ranges(range_value)
            if is_private_use(codepoint)
        )
    return text_glyphs, icon_codepoints


def describe_characters(characters: Iterable[str]) -> str:
    return "".join(sorted(characters, key=ord)) or "(none)"


def check_fonts(
    expected: dict[int, tuple[set[str], set[int]]],
    origins: dict[str, set[str]],
) -> int:
    failed = False
    for size, relative_output in FONT_OUTPUTS.items():
        expected_text, expected_icons = expected[size]
        output = PROJECT_DIR / relative_output
        actual_text, actual_icons = generated_options(output)
        missing_text = expected_text - actual_text
        extra_text = actual_text - expected_text
        missing_icons = expected_icons - actual_icons
        extra_icons = actual_icons - expected_icons

        if missing_text or extra_text or missing_icons or extra_icons:
            failed = True
            print(f"ui_font_zh_{size} does not match current UI sources:", file=sys.stderr)
            print(
                f"  missing text: {describe_characters(missing_text)}",
                file=sys.stderr,
            )
            print(
                f"  extra text:   {describe_characters(extra_text)}",
                file=sys.stderr,
            )
            print(
                f"  missing icons: {format_ranges(missing_icons) or '(none)'}",
                file=sys.stderr,
            )
            print(
                f"  extra icons:   {format_ranges(extra_icons) or '(none)'}",
                file=sys.stderr,
            )
            for character in sorted(missing_text, key=ord):
                print(
                    f"    {character} required by {', '.join(sorted(origins[character]))}",
                    file=sys.stderr,
                )

    if failed:
        print(
            "Run: python3 tools/generate_ui_fonts.py",
            file=sys.stderr,
        )
        return 1

    details = ", ".join(
        f"{size}px={len(text)} text/{len(icons)} icons"
        for size, (text, icons) in sorted(expected.items())
    )
    print(f"UI font coverage passed: {details}")
    return 0


def generate_fonts(expected: dict[int, tuple[set[str], set[int]]]) -> None:
    for required in (TEXT_FONT, ICON_FONT):
        if not (PROJECT_DIR / required).is_file():
            raise FileNotFoundError(required)

    for size, output in FONT_OUTPUTS.items():
        text_glyphs, icon_codepoints = expected[size]
        symbols = "".join(sorted(text_glyphs, key=ord))
        icon_ranges = format_ranges(icon_codepoints)
        command = [
            "npx",
            "--yes",
            "lv_font_conv@1.5.3",
            "--size",
            str(size),
            "--bpp",
            "4",
            "--format",
            "lvgl",
            "--lv-include",
            "lvgl.h",
            "--lv-font-name",
            f"ui_font_zh_{size}",
            "-o",
            str(output),
            "--font",
            str(TEXT_FONT),
            "-r",
            "0x20-0x7F",
            "--symbols",
            symbols,
            "--font",
            str(ICON_FONT),
            "-r",
            icon_ranges,
        ]
        print(f"Generating {output} ({len(text_glyphs)} text glyphs, "
              f"{len(icon_codepoints)} icons)")
        subprocess.run(command, cwd=PROJECT_DIR, check=True)


def codepoint_ranges(codepoints: Iterable[int]) -> list[tuple[int, int]]:
    ordered = sorted(set(codepoints))
    if not ordered:
        return []
    ranges: list[tuple[int, int]] = []
    start = previous = ordered[0]
    for codepoint in ordered[1:]:
        if codepoint == previous + 1:
            previous = codepoint
            continue
        ranges.append((start, previous))
        start = previous = codepoint
    ranges.append((start, previous))
    return ranges


def charset_include_content() -> str:
    accepted = {ord(character) for character in gb2312_text_glyphs()}
    accepted.update(plugin_icon_codepoints())
    ranges = codepoint_ranges(accepted)
    rows = "\n".join(
        f"    {{ 0x{start:04X}U, 0x{end:04X}U }},"
        for start, end in ranges
    )
    return (
        "/* Generated by tools/generate_ui_fonts.py. Do not edit. */\n"
        "static const plugin_charset_range_t PLUGIN_CHARSET_RANGES[] = {\n"
        f"{rows}\n"
        "};\n"
        "#define PLUGIN_CHARSET_RANGE_COUNT "
        "(sizeof(PLUGIN_CHARSET_RANGES) / sizeof(PLUGIN_CHARSET_RANGES[0]))\n"
    )


def generate_charset_include() -> None:
    output = PROJECT_DIR / CHARSET_OUTPUT
    output.write_text(charset_include_content(), encoding="utf-8")
    print(f"Generating {CHARSET_OUTPUT} ({len(gb2312_text_glyphs())} GB2312 glyphs)")


def check_charset_include() -> bool:
    output = PROJECT_DIR / CHARSET_OUTPUT
    return output.is_file() and output.read_text(encoding="utf-8") == charset_include_content()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify generated font options without invoking lv_font_conv",
    )
    args = parser.parse_args()

    system_text, system_icons, origins = collect_requirements()
    public_text = set(gb2312_text_glyphs()) | system_text
    public_icons = set(plugin_icon_codepoints())
    expected = {
        14: (public_text, public_icons),
        18: (system_text, system_icons),
    }
    if args.check:
        result = check_fonts(expected, origins)
        if not check_charset_include():
            print(
                f"{CHARSET_OUTPUT} does not match the public plugin charset",
                file=sys.stderr,
            )
            print("Run: python3 tools/generate_ui_fonts.py", file=sys.stderr)
            result = 1
        return result

    generate_fonts(expected)
    generate_charset_include()
    return check_fonts(expected, origins)


if __name__ == "__main__":
    raise SystemExit(main())
