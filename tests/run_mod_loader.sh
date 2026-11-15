#!/bin/sh
# Headless checks for --list-mods, --mod-dir, --mods-conf, real PNG loading,
# and the data-mod CLI integration.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
CC=${CC:-cc}
MODS=$ROOT/tests/fixtures/mods

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
mkdir -p "$TMP/home"

run ()
{
    env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" "$BIN" "$@"
}

fail ()
{
    echo "FAIL: $*" >&2
    exit 1
}

listing=$(run --list-mods --mod-dir "$MODS/valid")
[ "$listing" = "test_park@1.0.0 enabled Test Park" ] \
    || fail "unexpected --list-mods output: $listing"

listing=$(run --list-mods --mod-dir "$MODS/config")
case "$listing" in
    *"config_a@1.0.0 enabled Config A"*"config_b@1.0.0 enabled Config B"*|*"config_b@1.0.0 enabled Config B"*"config_a@1.0.0 enabled Config A"*) ;;
    *) fail "config listing did not show both mods: $listing" ;;
esac

printf 'config_b = 1\nconfig_a = 0\n' >"$TMP/mods.conf"
listing=$(run --list-mods --mod-dir "$MODS/config" --mods-conf "$TMP/mods.conf")
case "$listing" in
    *"config_b@1.0.0 enabled Config B"*) ;;
    *) fail "enabled mod missing from listing: $listing" ;;
esac
case "$listing" in
    *"config_a@1.0.0 disabled Config A"*) ;;
    *) fail "disabled mod missing from listing: $listing" ;;
esac

run --mod-dir "$MODS/valid" --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/mod.scn" --ticks 1 --seed 1 >"$TMP/mod.log" 2>&1 \
    || { cat "$TMP/mod.log" >&2; fail "batch run with a valid mod failed"; }

mkdir -p "$TMP/unsafe/unsafe_mod"
cp "$MODS/valid/valid_mod/park.png" "$TMP/unsafe/unsafe_mod/park.png"
cat >"$TMP/unsafe/unsafe_mod/mod.ini" <<'EOF'
[mod]
id = unsafe_residence
name = Unsafe Residence
version = 1.0.0
api = 1

[group:house]
name = Unsafe House
behavior = residence
size = 2
graphic = park.png
EOF
if run --mod-dir "$TMP/unsafe" --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/unsafe.scn" --ticks 0 --seed 1 >"$TMP/unsafe.log" 2>&1; then
    fail "a data mod was allowed to use the residence behavior"
fi
grep -q "not safe for mods" "$TMP/unsafe.log" \
    || { cat "$TMP/unsafe.log" >&2; fail "unsafe behavior was not reported"; }
run --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/base.scn" --ticks 1 --seed 1 >"$TMP/base.log" 2>&1 \
    || { cat "$TMP/base.log" >&2; fail "batch baseline run failed"; }
# The versioned mod block records the enabled mod, so it is expected to
# differ.  Compare the simulation payload below the header instead.
gzip -dc "$TMP/base.scn" | sed '1,/^end$/d' >"$TMP/base.payload"
gzip -dc "$TMP/mod.scn" | sed '1,/^end$/d' >"$TMP/mod.payload"
cmp -s "$TMP/base.payload" "$TMP/mod.payload" \
    || fail "loading a data-only mod changed base simulation output"

mkdir -p "$TMP/badmod/bad"
cat >"$TMP/badmod/bad/mod.ini" <<'EOF'
[mod]
id = bad_png
name = Bad PNG
version = 1.0.0
api = 1

[group:park]
name = Bad PNG Park
behavior = parkland
size = 1
graphic = bad.png
EOF
printf 'this is not a png\n' >"$TMP/badmod/bad/bad.png"
if run --mod-dir "$TMP/badmod" --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/bad.scn" --ticks 0 --seed 1 >"$TMP/bad.log" 2>&1; then
    fail "corrupt mod PNG was accepted"
fi
grep -q "graphic failed" "$TMP/bad.log" \
    || fail "corrupt PNG did not report a graphic failure"

if run --list-mods --mod-dir "$TMP/does-not-exist" >"$TMP/missing.log" 2>&1; then
    fail "missing explicit mod directory was accepted"
fi
grep -q "cannot open mod directory" "$TMP/missing.log" \
    || fail "missing mod directory did not report an error"

# Native plugin opt-in, dynamic loading, ABI validation, and registration.
PLUGIN_ROOT="$TMP/native"
mkdir -p "$PLUGIN_ROOT/native_mod"
cp "$MODS/valid/valid_mod/park.png" "$PLUGIN_ROOT/native_mod/park.png"
cat >"$PLUGIN_ROOT/native_mod/mod.ini" <<'EOF'
[mod]
id = native_test
name = Native Test
version = 1.0.0
api = 1
plugin-linux-x86_64 = test_plugin.so
EOF
"$CC" -shared -fPIC -I "$ROOT/src" -o "$PLUGIN_ROOT/native_mod/test_plugin.so" \
    "$ROOT/tests/test_plugin.c" || fail "native test plugin did not compile"

listing=$(run --list-mods --mod-dir "$PLUGIN_ROOT")
[ "$listing" = "native_test@1.0.0 enabled Native Test" ] \
    || fail "unexpected native --list-mods output: $listing"

if run --mod-dir "$PLUGIN_ROOT" --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/native-off.scn" --ticks 0 --seed 1 \
    >"$TMP/native-off.log" 2>&1; then
    fail "native plugin loaded without --native-plugins"
fi
grep -q "requires --native-plugins" "$TMP/native-off.log" \
    || { cat "$TMP/native-off.log" >&2; fail "missing native opt-in error"; }

run --native-plugins --mod-dir "$PLUGIN_ROOT" \
    --load "$ROOT/opening/good_times.scn" \
    --save "$TMP/native.scn" --ticks 0 --seed 1 >"$TMP/native.log" 2>&1 \
    || { cat "$TMP/native.log" >&2; fail "native plugin load failed"; }
gzip -dc "$TMP/native.scn" | grep -q 'type 400 native_test.park' \
    || fail "native type was not registered in the save header"

# Load a city with a 2x2 native building and run one real simulation tick.
# The callback exercises the host accessors and leaves its result in fields.
PYTHONPATH="$ROOT/tests" python3 - "$ROOT/opening/good_times.scn" \
    "$TMP/native-seed.gz" <<'PYNATIVE'
import gzip
import sys
from save_mod_header import read_save_header

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, "rb").read()
if raw[:2] == b"\x1f\x8b":
    raw = gzip.decompress(raw)
lines = raw.decode("utf-8").splitlines()
_, _, _, start = read_save_header(lines)

def put(x, y, offset, value, width=4):
    base = start + (x * 100 + y) * 42 + offset
    for i in range(width):
        lines[base + i] = str((value >> (8 * i)) & 0xff)

# Owned origin, then its three CST_USED sub-tiles.  Set a base flag on the
# origin to prove that plugin flag writes preserve it.
put(0, 0, 4, 0x10)
lines[start + 41] = "400"
for x, y in ((1, 0), (0, 1), (1, 1)):
    put(x, y, 12, 0)
    put(x, y, 16, 0)
    lines[start + (x * 100 + y) * 42 + 41] = "39"
with gzip.open(dst, "wt") as handle:
    handle.write("\n".join(lines) + "\n")
PYNATIVE

run --native-plugins --mod-dir "$PLUGIN_ROOT" \
    --load "$TMP/native-seed.gz" --save "$TMP/native-state.gz" \
    --ticks 1 --seed 1 >"$TMP/native-state.log" 2>&1 \
    || { cat "$TMP/native-state.log" >&2; fail "native simulation run failed"; }

PYTHONPATH="$ROOT/tests" python3 - "$TMP/native-state.gz" <<'PYCHECK'
import gzip
import sys
from save_mod_header import read_save_header

raw = gzip.decompress(open(sys.argv[1], "rb").read())
lines = raw.decode("utf-8").splitlines()
_, _, _, start = read_save_header(lines)

def get(x, y, offset, width=4):
    base = start + (x * 100 + y) * 42 + offset
    value = 0
    for i in range(width):
        value |= int(lines[base + i]) << (8 * i)
    return value

assert get(0, 0, 41, 1) == 400
assert get(0, 0, 4) == 0x10000010, hex(get(0, 0, 4))
assert get(0, 0, 0) == 7
assert get(0, 0, 8, 2) == 9
assert get(0, 0, 10, 2) == 10
assert get(0, 0, 12) == 123
assert get(0, 0, 16) == 124
assert get(0, 0, 20) == 111
assert get(0, 0, 28) == 1, get(0, 0, 28)
assert get(0, 0, 32) == 1
assert get(0, 0, 36) == 1
assert get(1, 0, 41, 1) == 39
assert get(1, 0, 12) == 0
PYCHECK

for variant in api size; do
    BAD_ROOT="$TMP/native-bad-$variant"
    mkdir -p "$BAD_ROOT/native_bad"
    cp "$MODS/valid/valid_mod/park.png" "$BAD_ROOT/native_bad/park.png"
    cat >"$BAD_ROOT/native_bad/mod.ini" <<'EOF'
[mod]
id = native_bad
name = Native Bad
version = 1.0.0
api = 1
plugin-linux-x86_64 = test_plugin_bad.so
EOF
    bad_flags=
    if [ "$variant" = size ]; then bad_flags=-DTEST_BAD_SIZE; fi
    "$CC" -shared -fPIC -I "$ROOT/src" $bad_flags \
        -o "$BAD_ROOT/native_bad/test_plugin_bad.so" \
        "$ROOT/tests/test_plugin_bad.c" || fail "bad native fixture did not compile"

    if run --native-plugins --mod-dir "$BAD_ROOT" \
        --load "$ROOT/opening/good_times.scn" \
        --save "$TMP/native-bad.scn" --ticks 0 --seed 1 \
        >"$TMP/native-bad.log" 2>&1; then
        fail "native plugin with bad $variant was accepted"
    fi
    if [ "$variant" = api ]; then
        grep -q "native plugin API" "$TMP/native-bad.log"
    else
        grep -q "native plugin ABI size" "$TMP/native-bad.log"
    fi || { cat "$TMP/native-bad.log" >&2; fail "bad $variant was not rejected as expected"; }
done

echo "PASS: data-mod CLI, listing, config, PNG integration, and native plugin loading"
