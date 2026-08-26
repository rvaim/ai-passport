#!/usr/bin/env python3
"""Generate and verify the shared 14 px Simplified Chinese UI font.

The public runtime accepts arbitrary plugin names and text, so built-in UI
strings alone are not a sufficient charset.  The generated font always covers
the 3,755 GB2312 level-one ideographs, then adds non-ASCII characters found in
the firmware's actual CMake source graph.
"""

from __future__ import annotations

import argparse
import ast
import re
import shlex
import subprocess
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
FONT_SOURCE = Path(
    "managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/"
    "NotoSansSC-Regular.ttf"
)
ICON_FONT_SOURCE = Path(
    "managed_components/lvgl__lvgl/scripts/built_in_font/"
    "FontAwesome5-Solid+Brands+Regular.woff"
)
FONT_OUTPUT = Path("components/passport_ui/src/passport_ui_font_zh_14.c")
FONT_NAME = "passport_ui_font_zh_14"
FONT_SIZE = 14
FONT_BPP = 4
ASCII_RANGE = "0x20-0x7E"
ICON_RANGE = "0xF077-0xF078"
ICON_GLYPH_COUNT = 2
COMMON_PUNCTUATION = "，。！？；：、“”‘’（）《》〈〉【】〔〕…—·￥～"
SDKCONFIG_DEFAULTS = Path("sdkconfig.defaults")
COMPRESSED_FONT_OPTION = "CONFIG_LV_USE_FONT_COMPRESSED"

C_STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"', re.DOTALL)
LOCAL_INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
OPTS_RE = re.compile(r"^ \* Opts: (.+)$", re.MULTILINE)
BITMAP_FORMAT_RE = re.compile(r"\.bitmap_format\s*=\s*(\d+)")


def gb2312_level_one_glyphs() -> frozenset[str]:
    """Return the 3,755 common ideographs in GB2312 rows 16 through 55."""
    glyphs: set[str] = set()
    for lead in range(0xB0, 0xD8):
        for trail in range(0xA1, 0xFF):
            try:
                glyphs.add(bytes((lead, trail)).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    if len(glyphs) != 3755:
        raise RuntimeError(f"unexpected GB2312 level-one glyph count: {len(glyphs)}")
    return frozenset(glyphs)


def _component_dirs(project_dir: Path) -> list[Path]:
    roots = [project_dir / "main"]
    components = project_dir / "components"
    if components.is_dir():
        roots.extend(path for path in sorted(components.iterdir()) if path.is_dir())
    return roots


def _compiled_sources(project_dir: Path) -> list[Path]:
    """Read literal source entries from each idf_component_register call."""
    generated = (project_dir / FONT_OUTPUT).resolve()
    files: list[Path] = []
    for root in _component_dirs(project_dir):
        cmake = root / "CMakeLists.txt"
        if not cmake.is_file():
            continue
        source = cmake.read_text(encoding="utf-8")
        match = re.search(
            r"\bSRCS\b(.*?)(?:\bINCLUDE_DIRS\b|\bPRIV_INCLUDE_DIRS\b|"
            r"\bREQUIRES\b|\bPRIV_REQUIRES\b|\))",
            source,
            re.DOTALL,
        )
        if match is None:
            continue
        for relative in re.findall(r'"([^"]+)"', match.group(1)):
            path = (root / relative).resolve()
            if path == generated:
                continue
            if path.suffix not in {".c", ".h"} or not path.is_file():
                raise FileNotFoundError(path)
            files.append(path)
    if not files:
        raise ValueError("no CMake source files found")
    return files


def source_files(project_dir: Path = PROJECT_DIR) -> list[Path]:
    """Return compiled sources and project-local headers reachable from them."""
    roots = _component_dirs(project_dir)
    include_dirs = [root for root in roots]
    include_dirs.extend(root / "include" for root in roots)
    include_dirs.extend(root / "src" for root in roots)
    project_root = project_dir.resolve()
    generated = (project_dir / FONT_OUTPUT).resolve()
    pending = _compiled_sources(project_dir)
    discovered: set[Path] = set()

    while pending:
        path = pending.pop().resolve()
        if path in discovered or path == generated:
            continue
        discovered.add(path)
        source = path.read_text(encoding="utf-8")
        for include in LOCAL_INCLUDE_RE.findall(source):
            candidates = [path.parent / include]
            candidates.extend(directory / include for directory in include_dirs)
            for candidate in candidates:
                candidate = candidate.resolve()
                try:
                    candidate.relative_to(project_root)
                except ValueError:
                    continue
                if candidate.is_file() and candidate.suffix == ".h":
                    pending.append(candidate)
                    break
    return sorted(discovered)


def _decode_c_literal(literal: str) -> str:
    value = ast.literal_eval(literal)
    if not isinstance(value, str):
        raise TypeError(f"unexpected non-string C literal: {literal}")
    return value


def source_glyphs(project_dir: Path = PROJECT_DIR) -> frozenset[str]:
    glyphs: set[str] = set(COMMON_PUNCTUATION)
    for path in source_files(project_dir):
        source = path.read_text(encoding="utf-8")
        for match in C_STRING_RE.finditer(source):
            try:
                value = _decode_c_literal(match.group(0))
            except (SyntaxError, ValueError):
                continue
            glyphs.update(
                character
                for character in value
                if ord(character) > 0x7F
                and character.isprintable()
                and not character.isspace()
                and not 0xE000 <= ord(character) <= 0xF8FF
            )
    return frozenset(glyphs)


def required_glyphs(project_dir: Path = PROJECT_DIR) -> frozenset[str]:
    return gb2312_level_one_glyphs() | source_glyphs(project_dir)


def _generated_options(path: Path) -> list[str]:
    match = OPTS_RE.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"missing lv_font_conv options in {path}")
    return shlex.split(match.group(1))


def _option_value(options: list[str], name: str) -> str | None:
    try:
        return options[options.index(name) + 1]
    except (ValueError, IndexError):
        return None


def _option_values(options: list[str], name: str) -> list[str]:
    """Return every value belonging to a repeated lv_font_conv option."""
    return [
        options[index + 1]
        for index, option in enumerate(options[:-1])
        if option == name
    ]


def generated_bitmap_format(path: Path) -> int:
    """Return the single bitmap format declared by an LVGL C font."""
    formats = {int(value) for value in BITMAP_FORMAT_RE.findall(path.read_text(encoding="utf-8"))}
    if len(formats) != 1:
        raise ValueError(f"expected one bitmap_format in {path}, found {sorted(formats)}")
    return formats.pop()


def sdkconfig_option_enabled(project_dir: Path, option: str) -> bool:
    """Return whether an sdkconfig default is explicitly enabled."""
    config = project_dir / SDKCONFIG_DEFAULTS
    if not config.is_file():
        return False
    return re.search(
        rf"^{re.escape(option)}=y$", config.read_text(encoding="utf-8"), re.MULTILINE
    ) is not None


def check_font(project_dir: Path = PROJECT_DIR) -> int:
    output = project_dir / FONT_OUTPUT
    if not output.is_file():
        print(f"missing generated font: {FONT_OUTPUT}", file=sys.stderr)
        return 1

    expected = required_glyphs(project_dir)
    options = _generated_options(output)
    actual = set(_option_value(options, "--symbols") or "")
    expected_options = {
        "--size": str(FONT_SIZE),
        "--bpp": str(FONT_BPP),
        "--format": "lvgl",
        "--lv-font-name": FONT_NAME,
    }
    errors = [
        f"{name} must be {value}"
        for name, value in expected_options.items()
        if _option_value(options, name) != value
    ]
    if "--no-kerning" not in options:
        errors.append("--no-kerning must be enabled")
    expected_fonts = [str(FONT_SOURCE), str(ICON_FONT_SOURCE)]
    actual_fonts = _option_values(options, "--font")
    if actual_fonts != expected_fonts:
        errors.append(f"--font must be {expected_fonts}, got {actual_fonts}")
    expected_ranges = [ASCII_RANGE, ICON_RANGE]
    actual_ranges = _option_values(options, "-r")
    if actual_ranges != expected_ranges:
        errors.append(f"-r must be {expected_ranges}, got {actual_ranges}")

    try:
        bitmap_format = generated_bitmap_format(output)
    except ValueError as error:
        errors.append(str(error))
    else:
        if bitmap_format != 0 and not sdkconfig_option_enabled(
            project_dir, COMPRESSED_FONT_OPTION
        ):
            errors.append(
                f"{COMPRESSED_FONT_OPTION}=y is required for compressed "
                f"bitmap_format={bitmap_format}"
            )

    missing = expected - actual
    extra = actual - expected
    if missing:
        errors.append("missing glyphs: " + "".join(sorted(missing, key=ord)))
    if extra:
        errors.append("extra glyphs: " + "".join(sorted(extra, key=ord)))

    if errors:
        for error in errors:
            print(f"UI font check failed: {error}", file=sys.stderr)
        print("Run: python3 tools/generate_ui_font.py", file=sys.stderr)
        return 1

    common = gb2312_level_one_glyphs()
    print(
        "UI font coverage: PASS "
        f"({len(common)} common Chinese, {len(expected - common)} additional, "
        f"{ICON_GLYPH_COUNT} action icons, "
        f"{len(expected) + 95 + ICON_GLYPH_COUNT} total glyphs)"
    )
    return 0


def generate_font(project_dir: Path = PROJECT_DIR) -> None:
    source = project_dir / FONT_SOURCE
    icon_source = project_dir / ICON_FONT_SOURCE
    output = project_dir / FONT_OUTPUT
    if not source.is_file():
        raise FileNotFoundError(source)
    if not icon_source.is_file():
        raise FileNotFoundError(icon_source)
    output.parent.mkdir(parents=True, exist_ok=True)
    symbols = "".join(sorted(required_glyphs(project_dir), key=ord))
    command = [
        "npx",
        "--yes",
        "lv_font_conv@1.5.3",
        "--size",
        str(FONT_SIZE),
        "--bpp",
        str(FONT_BPP),
        "--format",
        "lvgl",
        "--lv-include",
        "lvgl.h",
        "--lv-font-name",
        FONT_NAME,
        "-o",
        str(FONT_OUTPUT),
        "--font",
        str(FONT_SOURCE),
        "-r",
        ASCII_RANGE,
        "--symbols",
        symbols,
        "--font",
        str(ICON_FONT_SOURCE),
        "-r",
        ICON_RANGE,
        "--no-kerning",
    ]
    print(
        f"Generating {FONT_OUTPUT} with "
        f"{len(symbols) + 95 + ICON_GLYPH_COUNT} glyphs"
    )
    subprocess.run(command, cwd=project_dir, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed font without invoking lv_font_conv",
    )
    args = parser.parse_args()
    if args.check:
        return check_font()
    generate_font()
    return check_font()


if __name__ == "__main__":
    raise SystemExit(main())
