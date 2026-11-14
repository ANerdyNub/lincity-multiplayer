#!/bin/sh
# M4: versioned mod save header, old-save load, same-mod roundtrip, stable
# type remapping when load order changes, and missing/incompatible mod
# rejection with rollback.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
MODS=$ROOT/tests/fixtures/mods/valid
CONFIG=$ROOT/tests/fixtures/mods/config

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN]" >&2; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 2; }
case "$BIN" in
    /*) ;;
    *) BIN=$(command -v "$BIN" 2>/dev/null || printf '%s/%s' "$ROOT" "$BIN") ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home" "$TMP/empty"

run ()
{
    env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" "$BIN" "$@"
}

fail ()
{
    echo "FAIL: $*" >&2
    exit 1
}

if gzip -t "$ROOT/opening/good_times.scn" >/dev/null 2>&1; then
    cp "$ROOT/opening/good_times.scn" "$TMP/base.gz"
else
    gzip -c "$ROOT/opening/good_times.scn" >"$TMP/base.gz"
fi

# Patch two cells so the map contains mod types.  Saves store type IDs only;
# the registry derives the group from the current type mapping.
python3 - "$TMP/base.gz" "$TMP/mod_seed.gz" "$TMP/config_seed.gz" <<'PY'
import gzip
import sys

src, mod_path, config_path = sys.argv[1], sys.argv[2], sys.argv[3]
data = gzip.decompress(open(src, "rb").read()).decode("utf-8").splitlines()
if int(data[0]) >= 113:
    raise SystemExit("expected an old-format seed save")

def patch(changes):
    lines = list(data)
    for x, y, saved_type in changes:
        lines[1 + (x * 100 + y) * 42 + 41] = str(saved_type)
    return lines

def write(path, lines):
    with gzip.open(path, "wt") as handle:
        handle.write("\n".join(lines) + "\n")

write(mod_path, patch([(0, 0, 400)]))
write(config_path, patch([(0, 0, 400), (1, 1, 401)]))
PY

# Old saves have no mod block and still load; the writer adds a base-only
# version 113 block.
run --mod-dir "$TMP/empty" --load "$TMP/base.gz" --save "$TMP/old1.gz" \
    --ticks 0 >"$TMP/old.log" 2>&1 \
    || { cat "$TMP/old.log" >&2; fail "old save did not load"; }
run --mod-dir "$TMP/empty" --load "$TMP/old1.gz" --save "$TMP/old2.gz" \
    --ticks 0 >"$TMP/old2.log" 2>&1 \
    || { cat "$TMP/old2.log" >&2; fail "new base save did not reload"; }
cmp -s "$TMP/old1.gz" "$TMP/old2.gz" \
    || fail "base-only round trip changed the file"

PYTHONPATH="$ROOT/tests" python3 - "$TMP/old1.gz" <<'PY'
import gzip
import sys
from save_mod_header import read_save_header

raw = open(sys.argv[1], "rb").read()
if raw[:2] == b"\x1f\x8b":
    raw = gzip.decompress(raw)
lines = raw.decode("utf-8").splitlines()
version, mods, types, pos = read_save_header(lines)
assert version == 113, version
assert mods == [], mods
assert types.get(0) == "base.type.0", types.get(0)
assert all(key.startswith("base.type.") for key in types.values())
assert lines[pos].lstrip("-").isdigit()
PY

# Same mod, same load order: load/save is stable and the header records the
# mod and the sparse type IDs.
run --mod-dir "$MODS" --load "$TMP/mod_seed.gz" --save "$TMP/mod1.gz" \
    --ticks 0 >"$TMP/mod1.log" 2>&1 \
    || { cat "$TMP/mod1.log" >&2; fail "mod save did not load"; }
run --mod-dir "$MODS" --load "$TMP/mod1.gz" --save "$TMP/mod2.gz" \
    --ticks 0 >"$TMP/mod2.log" 2>&1 \
    || { cat "$TMP/mod2.log" >&2; fail "same-mod reload failed"; }
run --mod-dir "$MODS" --load "$TMP/mod2.gz" --save "$TMP/mod3.gz" \
    --ticks 0 >"$TMP/mod3.log" 2>&1 \
    || { cat "$TMP/mod3.log" >&2; fail "second same-mod reload failed"; }
cmp -s "$TMP/mod2.gz" "$TMP/mod3.gz" \
    || fail "same-mod round trip changed the file"

PYTHONPATH="$ROOT/tests" python3 - "$TMP/mod1.gz" <<'PY'
import gzip
import sys
from save_mod_header import read_save_header

raw = open(sys.argv[1], "rb").read()
if raw[:2] == b"\x1f\x8b":
    raw = gzip.decompress(raw)
lines = raw.decode("utf-8").splitlines()
version, mods, types, pos = read_save_header(lines)
assert version == 113, version
assert mods == [("test_park", "1.0.0", 1)], mods
assert types.get(400) == "test_park.park", types.get(400)
assert types.get(401) == "test_park.park_alt", types.get(401)
assert types.get(0) == "base.type.0", types.get(0)
assert lines[pos + 41] == "400", lines[pos + 41]
PY

# Missing mod: refuse before the map is read and do not create an output.
if run --mod-dir "$TMP/empty" --load "$TMP/mod1.gz" --save "$TMP/nomod.gz" \
    --ticks 0 >"$TMP/nomod.log" 2>&1; then
    fail "save loaded without its required mod"
fi
grep -Fq "requires mod test_park@1.0.0" "$TMP/nomod.log" \
    || { cat "$TMP/nomod.log" >&2; fail "missing mod did not name the requirement"; }
[ ! -e "$TMP/nomod.gz" ] || fail "failed load left an output save behind"

# Rollback: a missing-mod reload after a good city must not change the city.
run --mod-dir "$TMP/empty" --load "$TMP/base.gz" --save "$TMP/control.gz" \
    --ticks 0 >"$TMP/control.log" 2>&1 \
    || { cat "$TMP/control.log" >&2; fail "control load failed"; }
run --mod-dir "$TMP/empty" --load "$TMP/base.gz" --bad-load "$TMP/mod1.gz" \
    --save "$TMP/rolled.gz" --ticks 0 >"$TMP/rolled.log" 2>&1 \
    || { cat "$TMP/rolled.log" >&2; fail "rollback run failed"; }
grep -Fq "requires mod test_park@1.0.0" "$TMP/rolled.log" \
    || { cat "$TMP/rolled.log" >&2; fail "bad load did not fail for the missing mod"; }
cmp -s "$TMP/control.gz" "$TMP/rolled.gz" \
    || fail "failed mod load changed the running city"

# Same mod ID with an incompatible version is refused as well.
mkdir -p "$TMP/versioned"
cp -R "$MODS/valid_mod" "$TMP/versioned/valid_mod"
sed 's/^version = 1.0.0$/version = 2.0.0/' "$TMP/versioned/valid_mod/mod.ini" \
    >"$TMP/versioned/valid_mod/mod.ini.new"
mv "$TMP/versioned/valid_mod/mod.ini.new" "$TMP/versioned/valid_mod/mod.ini"
if run --mod-dir "$TMP/versioned" --load "$TMP/mod1.gz" \
    --save "$TMP/versioned.gz" --ticks 0 >"$TMP/versioned.log" 2>&1; then
    fail "save loaded with an incompatible mod version"
fi
grep -Fq "version 2.0.0 is enabled" "$TMP/versioned.log" \
    || { cat "$TMP/versioned.log" >&2; fail "version mismatch was not reported"; }

# The mapped type key must still exist in the mod.
mkdir -p "$TMP/shortmod"
cp -R "$MODS/valid_mod" "$TMP/shortmod/valid_mod"
sed '/^\[type:park_alt\]/,$d' "$TMP/shortmod/valid_mod/mod.ini" \
    >"$TMP/shortmod/valid_mod/mod.ini.new"
mv "$TMP/shortmod/valid_mod/mod.ini.new" "$TMP/shortmod/valid_mod/mod.ini"
if run --mod-dir "$TMP/shortmod" --load "$TMP/mod1.gz" \
    --save "$TMP/shortmod.gz" --ticks 0 >"$TMP/shortmod.log" 2>&1; then
    fail "save loaded after a mapped type key disappeared"
fi
grep -Fq "test_park.park_alt" "$TMP/shortmod.log" \
    || { cat "$TMP/shortmod.log" >&2; fail "missing type key was not reported"; }

# Load order changes the numeric IDs.  The map must follow the keys, not the
# offsets: the mod at 400 in the first save becomes 401 when it loads second.
printf 'config_a = 1\nconfig_b = 1\n' >"$TMP/order_ab.conf"
printf 'config_b = 1\nconfig_a = 1\n' >"$TMP/order_ba.conf"
run --mod-dir "$CONFIG" --mods-conf "$TMP/order_ab.conf" \
    --load "$TMP/config_seed.gz" --save "$TMP/ab.gz" --ticks 0 \
    >"$TMP/ab.log" 2>&1 || { cat "$TMP/ab.log" >&2; fail "order A/B load failed"; }
run --mod-dir "$CONFIG" --mods-conf "$TMP/order_ba.conf" \
    --load "$TMP/ab.gz" --save "$TMP/ba.gz" --ticks 0 \
    >"$TMP/ba.log" 2>&1 || { cat "$TMP/ba.log" >&2; fail "order B/A reload failed"; }

PYTHONPATH="$ROOT/tests" python3 - "$TMP/ab.gz" "$TMP/ba.gz" <<'PY'
import gzip
import sys
from save_mod_header import read_save_header

def load(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    lines = raw.decode("utf-8").splitlines()
    version, mods, types, pos = read_save_header(lines)
    assert version == 113
    return types, pos, lines

ab_types, ab_pos, ab_lines = load(sys.argv[1])
ba_types, ba_pos, ba_lines = load(sys.argv[2])

def saved_type(types, lines, pos, x, y):
    return int(lines[pos + (x * 100 + y) * 42 + 41])

def key_at(types, lines, pos, x, y):
    return types[saved_type(types, lines, pos, x, y)]

assert key_at(ab_types, ab_lines, ab_pos, 0, 0) == "config_a.park"
assert key_at(ba_types, ba_lines, ba_pos, 0, 0) == "config_a.park"
assert key_at(ab_types, ab_lines, ab_pos, 1, 1) == "config_b.park"
assert key_at(ba_types, ba_lines, ba_pos, 1, 1) == "config_b.park"
# The IDs really moved; only the key-based remap keeps the city intact.
assert saved_type(ab_types, ab_lines, ab_pos, 0, 0) == 400
assert saved_type(ba_types, ba_lines, ba_pos, 0, 0) == 401
assert saved_type(ab_types, ab_lines, ab_pos, 1, 1) == 401
assert saved_type(ba_types, ba_lines, ba_pos, 1, 1) == 400
PY

echo "PASS: mod saves load old maps, round-trip, remap by key, and reject missing mods"
