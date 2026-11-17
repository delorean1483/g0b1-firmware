"""Package the resident loader and relocated app into a program-flash-only HEX."""
import argparse
from pathlib import Path
import struct
import zlib


def record(address, kind, data):
    raw = struct.pack(">BHB", len(data), address, kind) + data
    return ":" + (raw + bytes([-sum(raw) & 0xFF])).hex().upper()


def package(loader, app):
    if not 192 <= len(loader) <= 4096 or not 192 <= len(app) <= 12160 or len(app) % 4:
        raise ValueError("image exceeds its layout or has invalid alignment")
    for image, base in ((loader, 0x08000000), (app, 0x08001000)):
        sp, pc = struct.unpack_from("<II", image)
        if sp != 0x20000800 or not pc & 1 or not base + 192 <= pc < base + len(image):
            raise ValueError("vector table does not match the image address")
    metadata = struct.pack("<III", 0x314C5041, len(app), zlib.crc32(app))
    lines = [record(0, 4, b"\x08\x00")]
    for start, data in ((0, loader), (0x1000, app), (0x3F80, metadata)):
        for offset in range(0, len(data), 16):
            lines.append(record(start + offset, 0, data[offset:offset + 16]))
    return "\n".join(lines + [record(0, 1, b"")]) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("loader", "app", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    args.output.write_text(package(args.loader.read_bytes(), args.app.read_bytes()), encoding="ascii")
