#!/usr/bin/env python3
"""Generate a save containing every real base group (0..40).

Group 35 (CST_USED) comes from the sub-tiles of the multi-tile buildings.
Index 41 in main_groups is the "EOF" sentinel and is not a map group.
"""
import gzip
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tests'))
import save_roundtrip as sr  # noqa: E402

WORLD = 100
CELL_LINES = 42
MAX_SUB = 512
MAX_MKT = 512

# One representative type per group.  Group 35 (CST_USED) is supplied by the
# sub-tiles of the multi-tile buildings, and group 41 is the EOF sentinel.
GROUP_TYPE = {
    0: 'CST_GREEN',
    1: 'CST_POWERL_H_L',
    2: 'CST_POWERS_SOLAR',
    3: 'CST_SUBSTATION_G',
    4: 'CST_RESIDENCE_LL',
    5: 'CST_FARM_O0',
    6: 'CST_MARKET_EMPTY',
    7: 'CST_TRACK_LR',
    8: 'CST_COALMINE_EMPTY',
    9: 'CST_RAIL_LR',
    10: 'CST_POWERS_COAL_EMPTY',
    11: 'CST_ROAD_LR',
    12: 'CST_INDUSTRY_L_C',
    13: 'CST_UNIVERSITY',
    14: 'CST_COMMUNE_1',
    15: 'CST_OREMINE_1',
    16: 'CST_TIP_0',
    17: 'CST_EX_PORT',
    18: 'CST_INDUSTRY_H_C',
    19: 'CST_PARKLAND_PLANE',
    20: 'CST_RECYCLE',
    21: 'CST_WATER',
    22: 'CST_HEALTH',
    23: 'CST_ROCKET_1',
    24: 'CST_WINDMILL_1_G',
    25: 'CST_MONUMENT_0',
    26: 'CST_SCHOOL',
    27: 'CST_BLACKSMITH_0',
    28: 'CST_MILL_0',
    29: 'CST_POTTERY_0',
    30: 'CST_FIRESTATION_1',
    31: 'CST_CRICKET_1',
    32: 'CST_BURNT',
    33: 'CST_SHANTY',
    34: 'CST_FIRE_1',
    36: 'CST_RESIDENCE_ML',
    37: 'CST_RESIDENCE_HL',
    38: 'CST_RESIDENCE_LH',
    39: 'CST_RESIDENCE_MH',
    40: 'CST_RESIDENCE_HH',
}

SIZES = {
    0: 1, 1: 1, 2: 4, 3: 2, 4: 3, 5: 4, 6: 2, 7: 1, 8: 4, 9: 1,
    10: 4, 11: 1, 12: 3, 13: 3, 14: 4, 15: 4, 16: 4, 17: 4, 18: 4,
    19: 1, 20: 2, 21: 1, 22: 2, 23: 4, 24: 2, 25: 2, 26: 2, 27: 2,
    28: 2, 29: 2, 30: 2, 31: 2, 32: 1, 33: 2, 34: 1, 35: 1,
    36: 3, 37: 3, 38: 3, 39: 3, 40: 3,
}

FIELD = {
    'population': 0, 'flags': 4, 'coal': 8, 'ore': 10,
    'int_1': 12, 'int_2': 16, 'int_3': 20, 'int_4': 24,
    'int_5': 28, 'int_6': 32, 'int_7': 36, 'pollution': 40,
    'type': 41,
}


def type_ids():
    return sr.defines(ROOT / 'src/lctypes.h', 'CST_')


def cell_lines(cell):
    out = [0] * CELL_LINES
    for name, off in FIELD.items():
        value = cell[name]
        if name == 'type':
            out[off] = value
        elif name == 'pollution':
            out[off] = value
        elif name in ('coal', 'ore'):
            out[off] = value & 0xff
            out[off + 1] = (value >> 8) & 0xff
        else:
            for j in range(4):
                out[off + j] = (value >> (8 * j)) & 0xff
    return out


def main():
    template = ROOT / 'tests/fuzz_save_corpus/normal_a.scn.gz'
    out_path = ROOT / 'tests/fixtures/all_groups.scn.gz'
    raw = gzip.decompress(template.read_bytes()).decode('ascii').splitlines()
    version = int(raw[0])
    assert version == 112, version
    map_end = 1 + WORLD * WORLD * CELL_LINES
    tail = raw[map_end:]
    # Version 112 layout: origin(3), substations(512*2), count,
    # markets(512*2), count, then the unchanged scalar/tail section.
    assert len(tail) > 3 + MAX_SUB * 2 + 1 + MAX_MKT * 2 + 1
    substation_count_idx = 3 + MAX_SUB * 2
    market_start = substation_count_idx + 1
    market_count_idx = market_start + MAX_MKT * 2
    rest = tail[market_count_idx + 1:]

    cells = [[{name: 0 for name in FIELD} for _ in range(WORLD)]
             for _ in range(WORLD)]
    ids = type_ids()
    placements = {}
    groups = [g for g in range(41) if g != 35]
    for index, group in enumerate(groups):
        col = index % 8
        row = index // 8
        ox = 3 + col * 6
        oy = 3 + row * 6
        size = SIZES[group]
        assert 3 + size <= 100 and 3 + size <= 100
        t = ids[GROUP_TYPE[group]]
        assert sr.TYPE_GROUP[t] == 'GROUP_' + {
            0: 'BARE', 1: 'POWER_LINE', 2: 'SOLAR_POWER',
            3: 'SUBSTATION', 4: 'RESIDENCE_LL', 5: 'ORGANIC_FARM',
            6: 'MARKET', 7: 'TRACK', 8: 'COALMINE', 9: 'RAIL',
            10: 'COAL_POWER', 11: 'ROAD', 12: 'INDUSTRY_L',
            13: 'UNIVERSITY', 14: 'COMMUNE', 15: 'OREMINE', 16: 'TIP',
            17: 'PORT', 18: 'INDUSTRY_H', 19: 'PARKLAND', 20: 'RECYCLE',
            21: 'WATER', 22: 'HEALTH', 23: 'ROCKET', 24: 'WINDMILL',
            25: 'MONUMENT', 26: 'SCHOOL', 27: 'BLACKSMITH', 28: 'MILL',
            29: 'POTTERY', 30: 'FIRESTATION', 31: 'CRICKET', 32: 'BURNT',
            33: 'SHANTY', 34: 'FIRE', 36: 'RESIDENCE_ML',
            37: 'RESIDENCE_HL', 38: 'RESIDENCE_LH', 39: 'RESIDENCE_MH',
            40: 'RESIDENCE_HH',
        }[group]
        placements[group] = (ox, oy, size)
        for i in range(size):
            for j in range(size):
                c = cells[ox + i][oy + j]
                if i == 0 and j == 0:
                    c['type'] = t
                else:
                    c['type'] = ids['CST_USED']
                    c['int_1'] = ox
                    c['int_2'] = oy

    # Keep the map file order used by the game: x outer, y inner.
    lines = [str(version)]
    for x in range(WORLD):
        for y in range(WORLD):
            lines.extend(str(v) for v in cell_lines(cells[x][y]))

    market = placements[6]
    substation = placements[3]
    subx = [0] * MAX_SUB
    suby = [0] * MAX_SUB
    subx[0], suby[0] = substation[0], substation[1]
    mktx = [0] * MAX_MKT
    mkty = [0] * MAX_MKT
    mktx[0], mkty[0] = market[0], market[1]

    lines.append(str(substation[0]))  # origin x
    lines.append(str(substation[1]))  # origin y
    lines.append('0')                  # total_time
    for i in range(MAX_SUB):
        lines.append(str(subx[i]))
        lines.append(str(suby[i]))
    lines.append('1')                  # num substations
    for i in range(MAX_MKT):
        lines.append(str(mktx[i]))
        lines.append(str(mkty[i]))
    lines.append('1')                  # num markets
    lines.extend(rest)

    data = ('\n'.join(lines) + '\n').encode('ascii')
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(gzip.compress(data, mtime=0))
    print(f'wrote {out_path} ({len(data)} bytes uncompressed)')
    print('placements:', placements)


if __name__ == '__main__':
    main()
