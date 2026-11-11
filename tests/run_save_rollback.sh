#!/bin/sh
# Transactional load test, using the headless --load/--bad-load/--save hooks.
# A valid city is loaded, a known-bad save is rejected, and the city is then
# saved; the result must equal the control saved without the failed reload.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-}
FIXTURE=$ROOT/opening/good_times.scn

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --fixture) FIXTURE=$2; shift 2 ;;
    --display) shift 2 ;;  # accepted for old callers; headless mode ignores it
    *) echo "usage: $0 [--binary BIN] [--fixture SAVE]" >&2; exit 2 ;;
    esac
done
[ -n "$BIN" ] && [ -x "$BIN" ] || {
    echo "set LINCITY_ASAN_BIN (or --binary) to a build" >&2
    exit 2
}
case "$BIN" in
    /*) ;;
    *) BIN=$(command -v "$BIN" 2>/dev/null || printf '%s/%s' "$ROOT" "$BIN") ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home"

run_batch () {
    env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" "$BIN" \
        --load "$1" --save "$2" --ticks 0 ${3:+--bad-load "$3"} \
        >"$TMP/batch.log" 2>&1
}

if gzip -t "$FIXTURE" >/dev/null 2>&1; then
    cp "$FIXTURE" "$TMP/seed.gz"
else
    gzip -c "$FIXTURE" > "$TMP/seed.gz"
fi

# A is the current-writer control.  D is a second load/save of A.
run_batch "$TMP/seed.gz" "$TMP/A.gz"
run_batch "$TMP/A.gz" "$TMP/D.gz"

python3 - "$TMP/A.gz" "$TMP" <<'PY'
import gzip
import os
import sys

src, out = sys.argv[1], sys.argv[2]
with gzip.open(src, "rt") as f:
    lines = f.read().splitlines()
map_end = 1 + 10000 * 42
sub_count = map_end + 3 + 512 * 2
market_count = sub_count + 1 + 512 * 2
scalar = market_count + 1
if len(lines) <= scalar:
    raise SystemExit("A has only %d lines" % len(lines))

def write(name, data):
    with gzip.open(os.path.join(out, "b_" + name + ".gz"), "wt") as f:
        f.write("\n".join(data) + "\n")

write("early_map", lines[:1000])
write("late_map", lines[:420000])
changed = list(lines)
changed[sub_count] = "513"
write("substation_count", changed)
changed = list(lines)
changed[market_count] = "513"
write("market_count", changed)
changed = list(lines)
changed[scalar] = "not-an-int"
write("final_scalar", changed)
PY

for variant in early_map late_map market_count substation_count final_scalar; do
    if run_batch "$TMP/A.gz" "$TMP/C.gz" "$TMP/b_$variant.gz"; then
        :
    else
        echo "FAIL: $variant was not rejected by the headless loader" >&2
        cat "$TMP/batch.log" >&2
        exit 1
    fi
    grep -Fq 'truncated or invalid save file' "$TMP/batch.log" || {
        echo "FAIL: $variant did not log the loader error" >&2
        cat "$TMP/batch.log" >&2
        exit 1
    }
    gzip -dc "$TMP/D.gz" > "$TMP/D.txt"
    gzip -dc "$TMP/C.gz" > "$TMP/C.txt"
    if ! diff -u "$TMP/D.txt" "$TMP/C.txt" > "$TMP/diff.txt"; then
        echo "FAIL: $variant left state differences after the failed load" >&2
        head -80 "$TMP/diff.txt" >&2
        exit 1
    fi
done

echo "PASS: 5 failed reloads preserved the control city"
