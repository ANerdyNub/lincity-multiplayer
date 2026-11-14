#!/usr/bin/env python3
"""Read the versioned mod block at the top of a LinCity save.

The block was introduced with save-format version 113.  Older saves start
with the map immediately after the version line.  read_save_header() returns
(version, mods, type_map, map_start) where mods is a list of
(id, version, api) tuples and type_map maps saved numeric IDs to stable keys.

Run as a script to print the line index where the map begins.
"""
import gzip
import sys
from pathlib import Path

SAVE_MOD_HEADER_SAVE_VERSION = 113
SAVE_MOD_HEADER_VERSION = 1


def read_save_header(lines):
    version = int(lines[0])
    if version < SAVE_MOD_HEADER_SAVE_VERSION:
        return version, [], {}, 1

    pos = 1

    def next_line():
        nonlocal pos
        if pos >= len(lines):
            raise ValueError("truncated mod header")
        line = lines[pos]
        pos += 1
        return line

    fields = next_line().split()
    if fields != ["#lcm", str(SAVE_MOD_HEADER_VERSION)]:
        raise ValueError("unsupported mod header: %r" % (fields,))

    fields = next_line().split()
    if len(fields) != 2 or fields[0] != "mods":
        raise ValueError("invalid mod header count: %r" % (fields,))
    mod_count = int(fields[1])
    if mod_count < 0:
        raise ValueError("negative mod count")
    mods = []
    for _ in range(mod_count):
        fields = next_line().split()
        if len(fields) != 4 or fields[0] != "mod":
            raise ValueError("invalid mod header entry: %r" % (fields,))
        mods.append((fields[1], fields[2], int(fields[3])))

    fields = next_line().split()
    if len(fields) != 2 or fields[0] != "types":
        raise ValueError("invalid type mapping count: %r" % (fields,))
    type_count = int(fields[1])
    if type_count < 0:
        raise ValueError("negative type mapping count")
    type_map = {}
    for _ in range(type_count):
        fields = next_line().split()
        if len(fields) != 3 or fields[0] != "type":
            raise ValueError("invalid type mapping entry: %r" % (fields,))
        saved = int(fields[1])
        if saved in type_map:
            raise ValueError("duplicate saved type ID %d" % saved)
        type_map[saved] = fields[2]

    if next_line() != "end":
        raise ValueError("missing mod header end marker")
    return version, mods, type_map, pos


def read_save_lines(path):
    raw = Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return raw.decode("utf-8").splitlines()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: %s SAVE" % sys.argv[0])
    lines = read_save_lines(sys.argv[1])
    _, _, _, map_start = read_save_header(lines)
    print(map_start)
    return 0


if __name__ == "__main__":
    sys.exit(main())
