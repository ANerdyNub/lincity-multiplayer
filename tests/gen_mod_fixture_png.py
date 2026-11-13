#!/usr/bin/env python3
"""Generate the tiny 16x16 palette PNGs used by the mod-loader fixtures."""
import struct
import sys
import zlib


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, palette, pixels):
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 3, 0, 0, 0)))
        fp.write(chunk(b"PLTE", bytes(palette)))
        fp.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        fp.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: gen_mod_fixture_png.py OUT.png INDEX")
    index = int(sys.argv[2], 0)
    palette = [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]
    pixels = [[index if (x + y) % 2 == 0 else 0 for x in range(16)]
              for y in range(16)]
    write_png(sys.argv[1], palette, pixels)


if __name__ == "__main__":
    main()
