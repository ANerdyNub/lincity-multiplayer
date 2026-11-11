#!/bin/sh
# Load and save through paths containing spaces and shell metacharacters.
# The old popen/system implementation executed the path as part of a shell
# command; the zlib implementation must treat it as an opaque filename.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN]" >&2; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 2; }

case "$BIN" in
    /*) ;;
    *) BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN") ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home"

SEED="$TMP/load ; touch injected_load; .scn.gz"
OUT="$TMP/save ; touch injected_save; .out.gz"
OUT2="$TMP/again ; touch injected_again; .out.gz"

if gzip -t "$ROOT/opening/good_times.scn" >/dev/null 2>&1; then
    cp "$ROOT/opening/good_times.scn" "$SEED"
else
    gzip -c "$ROOT/opening/good_times.scn" > "$SEED"
fi

if ! (cd "$TMP" && env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" \
    "$BIN" --load "$SEED" --save "$OUT" --ticks 0) >"$TMP/run.log" 2>&1; then
    echo "FAIL: headless save/load failed for a metacharacter path" >&2
    cat "$TMP/run.log" >&2
    exit 1
fi
[ -s "$OUT" ] || { echo "FAIL: save path was not created: $OUT" >&2; exit 1; }
gzip -t "$OUT"

if ! (cd "$TMP" && env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" \
    "$BIN" --load "$OUT" --save "$OUT2" --ticks 0) >"$TMP/reload.log" 2>&1; then
    echo "FAIL: reloading the metacharacter path failed" >&2
    cat "$TMP/reload.log" >&2
    exit 1
fi
[ -s "$OUT2" ] || { echo "FAIL: second save path was not created" >&2; exit 1; }
gzip -t "$OUT2"

if [ -e "$TMP/injected_load" ] || [ -e "$TMP/injected_save" ] \
    || [ -e "$TMP/injected_again" ]; then
    echo "FAIL: a save path was interpreted by a shell" >&2
    exit 1
fi

if command -v python3 >/dev/null 2>&1; then
    python3 "$ROOT/tests/save_roundtrip.py" "$ROOT/opening/good_times.scn" "$OUT"
fi

echo "PASS: gzip save/load round-trips paths with spaces and shell metacharacters"
