#!/bin/sh
# Run a multi-grid city for several in-game months with two builds and compare
# the complete saved power state: FLAG_POWERED tiles, grid membership of every
# power cell, grid composition, and substation assignments.  The baseline is a
# build whose src/power.c predates the P18 guards (for example 89c29a7); valid
# data must produce identical behavior.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CURRENT=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
BASELINE=
FIXTURE=$ROOT/opening/bad_times.scn
TICKS=300

while [ $# -gt 0 ]; do
    case "$1" in
    --current) CURRENT=$2; shift 2 ;;
    --baseline) BASELINE=$2; shift 2 ;;
    --fixture) FIXTURE=$2; shift 2 ;;
    --ticks) TICKS=$2; shift 2 ;;
    *) echo "usage: $0 --baseline BIN [--current BIN] [--fixture SAVE] [--ticks N]" >&2; exit 2 ;;
    esac
done
[ -x "$CURRENT" ] || { echo "not executable: $CURRENT" >&2; exit 2; }
[ -n "$BASELINE" ] && [ -x "$BASELINE" ] || {
    echo "set --baseline to the pre-P18 build" >&2
    exit 2
}

case "$CURRENT" in /*) ;; *) CURRENT=$(command -v "$CURRENT" 2>/dev/null || printf '%s/%s' "$ROOT" "$CURRENT") ;; esac
case "$BASELINE" in /*) ;; *) BASELINE=$(command -v "$BASELINE" 2>/dev/null || printf '%s/%s' "$ROOT" "$BASELINE") ;; esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home"
if gzip -t "$FIXTURE" >/dev/null 2>&1; then
    cp "$FIXTURE" "$TMP/seed.gz"
else
    gzip -c "$FIXTURE" > "$TMP/seed.gz"
fi

for build in current baseline; do
    bin=$CURRENT
    [ "$build" = baseline ] && bin=$BASELINE
    env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" \
        "$bin" --load "$TMP/seed.gz" --save "$TMP/$build.gz" \
        --ticks "$TICKS" >"$TMP/$build.log" 2>&1
done

python3 - "$TMP/current.gz" "$TMP/baseline.gz" "$TICKS" <<'PY'
import gzip
import sys

MAP_END = 1 + 10000 * 42
SUB_COUNT = MAP_END + 3 + 512 * 2
SCALARS = MAP_END + 3 + 512 * 2 + 1 + 512 * 2 + 1
WORLD = 100
CELL = 42
FLAG_POWERED = 0x10
POWER_LINE_TYPES = set(range(1, 23))
SUBSTATION_TYPES = set(range(50, 53))
WINDMILL_TYPES = set(range(190, 202))
SOURCE_TYPES = set(range(40, 45)) | WINDMILL_TYPES
POWER_ENTRY_TYPES = SUBSTATION_TYPES | WINDMILL_TYPES
POWER_TYPES = POWER_LINE_TYPES | SUBSTATION_TYPES | SOURCE_TYPES


def read(path):
    with gzip.open(path, "rt") as f:
        lines = f.read().splitlines()
    nsub = int(lines[SUB_COUNT])
    scalars = [int(x) for x in lines[SCALARS:SCALARS + 26]]

    def cell(x, y):
        return 1 + (x * WORLD + y) * CELL

    def int32(x, y, offset):
        base = cell(x, y) + offset
        return sum(int(lines[base + j]) << (8 * j) for j in range(4))

    # numof_substations includes both substations and windmills; get_power()
    # uses this list as its search set.  Validate the saved entries, but count
    # the actual map tiles separately so the report is not misleading.
    sub6 = []
    listed_substations = 0
    listed_windmills = 0
    for i in range(nsub):
        x = int(lines[MAP_END + 3 + i * 2])
        y = int(lines[MAP_END + 4 + i * 2])
        if not (0 <= x < WORLD and 0 <= y < WORLD):
            raise SystemExit("power entry %d has invalid coordinates" % i)
        typ = int(lines[cell(x, y) + 41])
        if typ not in POWER_ENTRY_TYPES:
            raise SystemExit("power entry %d (%d,%d) is not a grid entry"
                             % (i, x, y))
        gid = int32(x, y, 32)
        if gid <= 0:
            raise SystemExit("power entry %d (%d,%d) was unpowered"
                             % (i, x, y))
        sub6.append(gid)
        if typ in SUBSTATION_TYPES:
            listed_substations += 1
        else:
            listed_windmills += 1

    grids = {}
    assignments = {}
    powered_tiles = 0
    substation_tiles = 0
    for x in range(WORLD):
        for y in range(WORLD):
            typ = int(lines[cell(x, y) + 41])
            flags = int32(x, y, 4)
            if flags & FLAG_POWERED:
                powered_tiles += 1
            if typ in SUBSTATION_TYPES:
                substation_tiles += 1
            if typ not in POWER_TYPES:
                continue
            gid = int32(x, y, 32)
            if gid <= 0:
                raise SystemExit("power tile (%d,%d) has no grid" % (x, y))
            assignments[(x, y)] = gid
            state = grids.setdefault(
                gid, {"cells": 0, "subs": 0, "windmills": 0, "sources": 0})
            state["cells"] += 1
            if typ in SUBSTATION_TYPES:
                state["subs"] += 1
            if typ in WINDMILL_TYPES:
                state["windmills"] += 1
            if typ in SOURCE_TYPES:
                state["sources"] += 1

    for gid, state in grids.items():
        if state["subs"] and not state["sources"]:
            raise SystemExit("grid %d has substations but no source" % gid)

    if listed_substations != substation_tiles:
        raise SystemExit("saved power-entry list has %d substations; map has %d"
                         % (listed_substations, substation_tiles))

    return {
        "power_entries": nsub,
        "listed_substations": listed_substations,
        "listed_windmills": listed_windmills,
        "substation_tiles": substation_tiles,
        "sub6": sub6,
        "scalars": scalars,
        "powered_tiles": powered_tiles,
        "grids": grids,
        "assignments": assignments,
    }


cur = read(sys.argv[1])
base = read(sys.argv[2])
for label, old, new in (
        ("power entries", base["power_entries"], cur["power_entries"]),
        ("substation tiles", base["substation_tiles"],
         cur["substation_tiles"]),
        ("power-entry grids", base["sub6"], cur["sub6"]),
        ("FLAG_POWERED tiles", base["powered_tiles"], cur["powered_tiles"]),
        ("grid composition", base["grids"], cur["grids"]),
        ("power-cell grid assignments", base["assignments"], cur["assignments"]),
        ("legacy power_made", base["scalars"][16], cur["scalars"][16]),
        ("legacy power_used", base["scalars"][17], cur["scalars"][17])):
    if old != new:
        print("DIFF %s: baseline=%r current=%r" % (label, old, new))
        raise SystemExit(1)

print("PASS: %s ticks; grids=%d; powered_tiles=%d; substations=%d; "
      "windmills=%d; power_entries=%d; legacy power_made=%d power_used=%d" % (
          sys.argv[3], len(cur["grids"]), cur["powered_tiles"],
          cur["listed_substations"], cur["listed_windmills"],
          cur["power_entries"], cur["scalars"][16], cur["scalars"][17]))
for gid, state in sorted(cur["grids"].items()):
    print("  grid %d: cells=%d substations=%d windmills=%d sources=%d" % (
        gid, state["cells"], state["subs"], state["windmills"],
        state["sources"]))
print("note: power_made/power_used are legacy saved counters; no simulation "
      "code updates them, so they are not grid output or demand.")
PY
