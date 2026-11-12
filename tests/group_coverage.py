#!/usr/bin/env python3
"""Report which base groups appear in one or more save files.

The real base group IDs are 0..40.  main_groups[41] is the "EOF" sentinel
with size 0 and cannot occur in a map.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tests'))
import save_roundtrip as sr  # noqa: E402

REAL_GROUPS = 41


def group_defines():
    values = {}
    for name, value in sr.defines(ROOT / 'src/lin-city.h', 'GROUP_').items():
        values[name] = value
    return values


def group_names():
    text = (ROOT / 'src/lintypes.c').read_text()
    entries = re.findall(
        r'/\*\s*(\d+)\s*\*/\s*(?:/\*[^*]*\*/\s*)*'
        r'\{\s*(?:N_\()?"([^"]+)"', text)
    return {int(index): name for index, name in entries}


def scan(path, group_id, names):
    save = sr.read_save(path)
    present = set()
    unknown = {}
    for cell in save['map']:
        typ = cell[sr.FIELD0['type']]
        group = sr.TYPE_GROUP.get(typ)
        if group is None:
            unknown[typ] = unknown.get(typ, 0) + 1
            continue
        present.add(group_id[group])
    return present, unknown


def describe(present, names):
    missing = [i for i in range(REAL_GROUPS) if i not in present]
    text = '%d/%d base groups' % (len(present), REAL_GROUPS)
    if missing:
        text += '; missing ' + ', '.join(
            '%d:%s' % (i, names.get(i, '?')) for i in missing)
    return text, missing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves', nargs='+')
    parser.add_argument('--require-all', action='store_true',
                        help='fail unless every file/union contains groups 0..40')
    args = parser.parse_args()

    group_id = group_defines()
    names = group_names()
    union = set()
    failures = []

    for raw in args.saves:
        path = Path(raw)
        if not path.is_absolute():
            path = ROOT / path
        present, unknown = scan(path, group_id, names)
        union |= present
        text, missing = describe(present, names)
        print('%s: %s' % (path.name, text))
        if unknown:
            print('  unknown type IDs: %s' % ', '.join(
                '%d x%d' % (t, n) for t, n in sorted(unknown.items())))
        if args.require_all and missing:
            failures.append(path.name)

    if len(args.saves) > 1:
        text, missing = describe(union, names)
        print('union: %s' % text)
        if args.require_all and missing:
            failures.append('union')

    if failures:
        print('FAIL: missing base groups in %s' % ', '.join(failures))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
