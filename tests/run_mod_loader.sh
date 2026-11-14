#!/bin/sh
# Headless checks for --list-mods, --mod-dir, --mods-conf, real PNG loading,
# and the data-mod CLI integration.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
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

echo "PASS: data-mod CLI, listing, config, and PNG integration"
