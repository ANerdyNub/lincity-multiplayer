#!/usr/bin/env python3
"""Compare two uncompressed LinCity saves after load/save normalization.

The loader intentionally resets per-frame animation counters and rebuilds the
power-grid assignment, and older saves have shorter/obsolete sections.  Those
are the only fields this checker masks; every other map and scalar value must
match.
"""
import argparse
import re
import sys
from pathlib import Path

WORLD = 100
CELL_LINES = 42
MM_MS_C_VER = 97
MG_C_VER = 111
OLD_SUB = 100
MAX_SUB = 512
OLD_MKT = 100
MAX_MKT = 512
PBAR_DATA_SIZE = 12
NUM_PBARS = 9
NUMOF_MODULES = 32

# Map field byte offsets in the 42-line cell representation.
FIELD0 = {
    "population": 0, "flags": 4, "coal": 8, "ore": 10,
    "int_1": 12, "int_2": 16, "int_3": 20, "int_4": 24,
    "int_5": 28, "int_6": 32, "int_7": 36, "pollution": 40,
    "type": 41,
}
FIELD_SIZE = {
    "population": 4, "flags": 4, "coal": 2, "ore": 2,
    "int_1": 4, "int_2": 4, "int_3": 4, "int_4": 4,
    "int_5": 4, "int_6": 4, "int_7": 4,
}


def defines(path, prefix):
    out = {}
    for name, value in re.findall(
            r"^#define\s+(" + prefix + r"\w+)\s+(-?\d+)\b",
            Path(path).read_text(), re.M):
        out[name] = int(value)
    return out


def type_groups():
    constants = defines("src/lctypes.h", "CST_")
    groups = defines("src/lin-city.h", "GROUP_")
    mapping = {}
    for name, group in re.findall(
            r"\[(CST_\w+)\]\s*=\s*(GROUP_\w+)",
            Path("tests/fuzz_type_groups.h").read_text()):
        if name in constants:
            mapping[constants[name]] = group
    return mapping


TYPE_GROUP = type_groups()


def derived_offsets(group):
    d = set()
    if group and group.startswith("GROUP_RESIDENCE_"):
        d.add("int_3")
    if group in {
        "GROUP_WINDMILL", "GROUP_BLACKSMITH", "GROUP_MILL",
        "GROUP_POTTERY", "GROUP_CRICKET", "GROUP_FIRESTATION",
    }:
        d.add("int_4")
    if group == "GROUP_FIRE":
        d.update(("int_1", "int_3"))
    if group == "GROUP_COMMUNE":
        d.add("int_1")
    if group == "GROUP_ROCKET":
        d.add("int_5")
    if group == "GROUP_INDUSTRY_H":
        d.add("int_6")
    if group == "GROUP_INDUSTRY_L":
        d.add("int_7")
    # update_tech_dep() derives these from the saved tech level.
    if group == "GROUP_ORGANIC_FARM":
        d.add("int_7")
    if group in {"GROUP_WINDMILL", "GROUP_COAL_POWER"}:
        d.add("int_1")
    if group == "GROUP_SOLAR_POWER":
        d.add("int_3")
    # map_power_grid() rebuilds IDs/timestamps and power-line animation.
    if group in {
        "GROUP_POWER_LINE", "GROUP_SUBSTATION", "GROUP_SOLAR_POWER",
        "GROUP_COAL_POWER", "GROUP_WINDMILL",
    }:
        d.update(("int_6", "int_7"))
    if group == "GROUP_POWER_LINE":
        d.add("int_5")
    return d


def read_map(lines, pos):
    cells = []
    for _ in range(WORLD * WORLD):
        vals = [int(x) for x in lines[pos:pos + CELL_LINES]]
        if len(vals) != CELL_LINES:
            raise ValueError("truncated map")
        pos += CELL_LINES
        typ = vals[FIELD0["type"]]
        group = TYPE_GROUP.get(typ)
        mask = derived_offsets(group)
        canonical = [None] * CELL_LINES
        for name, start in FIELD0.items():
            size = FIELD_SIZE.get(name, 1)
            for j in range(size):
                if name not in mask:
                    canonical[start + j] = vals[start + j]
        # Light/dark power-line variants are representations of the same tile.
        if group == "GROUP_POWER_LINE" and typ >= 12 and typ <= 22:
            canonical[FIELD0["type"]] = typ - 11
        cells.append(tuple(canonical))
    return cells, pos


def read_save(path):
    import gzip
    raw = Path(path).read_bytes()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    lines = raw.decode("utf-8").splitlines()
    pos = 1
    version = int(lines[0])
    cells, pos = read_map(lines, pos)

    def take(n=1):
        nonlocal pos
        vals = [int(x) for x in lines[pos:pos + n]]
        if len(vals) != n:
            raise ValueError("truncated tail")
        pos += n
        return vals

    originx, originy, total_time = take(3)

    nsub_pairs = OLD_SUB if version <= MM_MS_C_VER else MAX_SUB
    subs = take(nsub_pairs * 2)
    nsub = take()[0]
    nmkt_pairs = OLD_MKT if version <= MM_MS_C_VER else MAX_MKT
    markets = take(nmkt_pairs * 2)
    nmkt = take()[0]

    scalars = take(26)
    if version <= MG_C_VER:
        msize = 120
    else:
        msize = take()[0]
    months = []
    for _ in range(msize):
        months.append(take(4))
        if version <= MG_C_VER:
            take(5)  # obsolete diffgraph entries

    # Parse the fixed-size tail backwards from its end.  The final seven
    # strings are compatibility dummies, then sust, CR, highest tech,
    # given_scene, one or two numeric dummies, module flags, stats, pbars,
    # and rockets.  Reading backwards avoids depending on monthgraph size.
    sust_idx = len(lines) - 8
    cr_idx = sust_idx - 1
    highest_idx = cr_idx - 1
    given_idx = highest_idx - 1
    dummy_count = 0
    while dummy_count < 2 and re.fullmatch(r"-?\d+", lines[given_idx - 1 - dummy_count].strip()):
        dummy_count += 1
    module_idx = given_idx - dummy_count - NUMOF_MODULES
    stats_idx = module_idx - 10
    pbar_tail_idx = stats_idx - 2 * NUM_PBARS
    pbar_data_idx = pbar_tail_idx - PBAR_DATA_SIZE * NUM_PBARS
    rockets_idx = pbar_data_idx - 3

    rockets = [int(x) for x in lines[rockets_idx:rockets_idx + 3]]
    pbar_data = [int(x) for x in lines[pbar_data_idx:pbar_tail_idx]]
    pbar_tail = [int(x) for x in lines[pbar_tail_idx:stats_idx]]
    stats = lines[stats_idx:module_idx]
    module_help = [int(x) for x in lines[module_idx:given_idx - dummy_count]]
    given_scene = ""
    highest_tech = 0
    cr = ""
    sust = ""

    return {
        "version": version,
        "map": cells,
        "origin": (originx, originy),
        "total_time": total_time,
        "substations": subs[:nsub * 2],
        "substation_count": nsub,
        "markets": markets[:nmkt * 2],
        "market_count": nmkt,
        "scalars": scalars,
        "monthgraph": months,
        "rockets": rockets,
        "pbar_data": pbar_data,
        "pbar_tail": pbar_tail,
        "stats": stats,
        "module_help": module_help,
        "given_scene": given_scene,
        "highest_tech": highest_tech,
        "cr": cr,
        "sust": sust,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("roundtrip")
    ap.add_argument("--expect", action="append", default=[],
                    metavar="INDEX=VALUE",
                    help="require a scalar value in the round-trip save")
    args = ap.parse_args()

    a = read_save(args.original)
    b = read_save(args.roundtrip)
    failures = []

    for key in ("total_time", "substation_count", "market_count",
                "substations", "markets"):
        if a[key] != b[key]:
            failures.append((key, a[key], b[key]))

    # The writer pads the monthgraph to the current screen width; compare the
    # saved values (the fourth field is the obsolete diffgraph).
    common = min(len(a["monthgraph"]), len(b["monthgraph"]))
    for i in range(common):
        if a["monthgraph"][i][:3] != b["monthgraph"][i][:3]:
            failures.append(("monthgraph", a["monthgraph"][:common],
                             b["monthgraph"][:common]))
            break
    # The 24th scalar is the removed diff_old_population; the 25th is the
    # old monthgraph size.  Both are obsolete/derived on load.
    for name, old, new in (("scalars", a["scalars"], b["scalars"]),
                           ("rockets", a["rockets"][1:], b["rockets"][1:])):
        if name == "scalars":
            if old[:24] != new[:24]:
                failures.append((name, old, new))
        elif old != new:
            failures.append((name, old, new))
    for key in ("pbar_data", "pbar_tail", "stats", "module_help"):
        if a[key] != b[key]:
            failures.append((key, a[key], b[key]))

    if a["map"] != b["map"]:
        diffs = [(i, x, y) for i, (x, y) in enumerate(zip(a["map"], b["map"]))
                 if x != y]
        failures.append(("map", diffs[:10], len(diffs)))

    for expression in args.expect:
        try:
            index, value = expression.split("=", 1)
            index, value = int(index), int(value)
        except ValueError:
            failures.append(("expect", expression, "INDEX=VALUE"))
            continue
        if index < 0 or index >= len(a["scalars"]):
            failures.append(("expect", expression, "index out of range"))
        elif a["scalars"][index] != value:
            failures.append(("expect scalar %d" % index,
                             a["scalars"][index], value))

    if failures:
        for key, old, new in failures:
            print("DIFF %s:" % key)
            if key == "map":
                for index, left, right in old:
                    y, x = divmod(index, WORLD)
                    print("  cell %d,%d %s -> %s" % (x, y, left, right))
                print("  %d differing cells" % new)
            else:
                print("  old=%r" % (old,))
                print("  new=%r" % (new,))
        return 1
    print("PASS: %s matches %s outside documented derived fields"
          % (args.original, args.roundtrip))
    return 0


if __name__ == "__main__":
    sys.exit(main())
