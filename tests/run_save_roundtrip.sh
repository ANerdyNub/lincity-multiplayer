#!/bin/sh
# Load every corpus save, write it back through the production writer, and
# compare the result with the original.  This is the headless replacement for
# the xdotool-based save tests.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
CORPUS=$ROOT/tests/fuzz_save_corpus

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --corpus) CORPUS=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN] [--corpus DIR]" >&2; exit 2 ;;
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

for name in good_times bad_times normal_a normal_d; do
    fixture=$CORPUS/$name.scn.gz
    [ -f "$fixture" ] || fixture=$CORPUS/$name.scn
    [ -f "$fixture" ] || { echo "missing fixture: $CORPUS/$name.scn(.gz)" >&2; exit 1; }
    if gzip -t "$fixture" >/dev/null 2>&1; then
        cp "$fixture" "$TMP/$name.gz"
    else
        gzip -c "$fixture" > "$TMP/$name.gz"
    fi

    if ! env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" \
        "$BIN" --load "$TMP/$name.gz" --save "$TMP/$name.out" \
        --ticks 0 >"$TMP/$name.log" 2>&1; then
        echo "FAIL: headless load/save failed for $name" >&2
        cat "$TMP/$name.log" >&2
        exit 1
    fi

    expect=
    if [ "$name" = bad_times ]; then
        # The 4a6f79d loader must preserve the saved accumulators.
        expect="--expect 12=235358 --expect 14=788"
    fi
    # shellcheck disable=SC2086
    python3 "$ROOT/tests/save_roundtrip.py" "$fixture" "$TMP/$name.out" $expect
done

echo "PASS: all corpus saves survive a headless round trip"
