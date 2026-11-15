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
