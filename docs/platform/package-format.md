<p align="right"><strong>English</strong> · <a href="package-format.zh_CN.md">简体中文</a></p>

# `.pap` Package Format v1

`.pap` is a sequential little-endian package designed for bounded-RAM streaming rather than ZIP extraction. It starts with a 16-byte header, followed by a UTF-8 JSON manifest, then file entries containing path length, flags, size, CRC-32, path bytes, and file data.

Absolute paths, traversal segments, empty segments, backslashes, oversized manifests, and excessive entry counts are rejected. Installation uses staging plus backup rename so validation finishes before the new package replaces the installed copy.
