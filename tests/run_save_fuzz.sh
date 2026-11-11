#!/bin/sh
# Load a save fixture in one save slot and check whether the game survives.
#
# Usage:
#   LINCITY_ASAN_BIN=/path/to/asan/xlincity DISPLAY=:99 \
#       tests/run_save_fuzz.sh [--expect crash|alive] [--fixture FILE] \
#           [--save-name NAME] [--set-line N VALUE] [--require-log TEXT] \
#           [--forbid-log TEXT]
#
# The default fixture is the audit bundle's 24-byte gzip of "112\n" (H3).
# `--set-line` gunzips a fixture, replaces one 1-based line, and re-gzips it.
# Requires xdotool and an X display.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-}
DISPLAY=${DISPLAY:-:99}
EXPECT="crash"
FIXTURE=
SAVE_NAME=2_trunc
SET_LINE=
SET_VALUE=
REQUIRE_LOG=
FORBID_LOG=

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --display) DISPLAY=$2; shift 2 ;;
    --expect) EXPECT=$2; shift 2 ;;
    --fixture) FIXTURE=$2; shift 2 ;;
    --save-name) SAVE_NAME=$2; shift 2 ;;
    --set-line) SET_LINE=$2; SET_VALUE=$3; shift 3 ;;
    --require-log) REQUIRE_LOG=$2; shift 2 ;;
    --forbid-log) FORBID_LOG=$2; shift 2 ;;
    *) echo "usage: $0 [--binary ASAN_BIN] [--display :N] [--expect crash|alive] [--fixture FILE] [--save-name NAME] [--set-line N VALUE] [--require-log TEXT] [--forbid-log TEXT]" >&2; exit 2 ;;
    esac
done

case "$EXPECT" in crash|alive) ;; *) echo "--expect must be crash or alive" >&2; exit 2 ;; esac
[ -n "$BIN" ] && [ -x "$BIN" ] || {
    echo "set LINCITY_ASAN_BIN (or --binary) to an ASan build" >&2
    exit 2
}
command -v xdotool >/dev/null 2>&1 || { echo "xdotool not found" >&2; exit 2; }
DISPLAY=$DISPLAY xdpyinfo >/dev/null 2>&1 || { echo "no X display at $DISPLAY" >&2; exit 2; }
case "$SAVE_NAME" in ''|*/*) echo "--save-name must be a file name" >&2; exit 2 ;; esac
SLOT=$(printf '%s' "$SAVE_NAME" | cut -c1)
case "$SLOT" in [1-9]) ;; *) echo "--save-name must start with a slot digit 1-9" >&2; exit 2 ;; esac

TMP=$(mktemp -d)
HOST_PID=
cleanup() {
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

mkdir -p "$TMP/.lincity"
SAVE_PATH=$TMP/.lincity/$SAVE_NAME
if [ -z "$FIXTURE" ]; then
    # The exact H3 fixture: gzip of the single line "112\n".
    printf '112\n' | gzip -c >"$SAVE_PATH"
elif [ -n "$SET_LINE" ]; then
    python3 - "$FIXTURE" "$SAVE_PATH" "$SET_LINE" "$SET_VALUE" <<'PY'
import gzip
import sys

src, dst, line_no, value = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
with gzip.open(src, "rt") as f:
    lines = f.read().splitlines()
if line_no < 1 or line_no > len(lines):
    raise SystemExit("fixture has %d lines; cannot set line %d" % (len(lines), line_no))
lines[line_no - 1] = value
with gzip.open(dst, "wt") as f:
    f.write("\n".join(lines) + "\n")
PY
else
    cp "$FIXTURE" "$SAVE_PATH"
fi

export DISPLAY
HOME=$TMP LINCITY_HOME=$ROOT "$BIN" >"$TMP/save.log" 2>&1 &
HOST_PID=$!

WIN=
i=0
while [ $i -lt 60 ]; do
    WIN=$(xdotool search --name 'xlincity' 2>/dev/null | tail -1 || true)
    [ -n "$WIN" ] && break
    kill -0 "$HOST_PID" 2>/dev/null || break
    sleep 0.2
    i=$((i + 1))
done
if [ -z "$WIN" ]; then
    echo "FAIL: game window did not appear" >&2
    cat "$TMP/save.log" >&2 || true
    exit 1
fi

xdotool windowfocus --sync "$WIN" 2>/dev/null || true
xdotool key --window "$WIN" --clearmodifiers Return   # dismiss splash
sleep 1.0
xdotool key --window "$WIN" --clearmodifiers Escape   # dismiss opening help
sleep 0.8

load_seen() {
    grep -Fq 'load_city: loaded' "$TMP/save.log" 2>/dev/null \
        || grep -Fq 'truncated or invalid save file' "$TMP/save.log" 2>/dev/null
}
try_load() {
    xdotool key --window "$WIN" --clearmodifiers l      # load dialog
    sleep 0.8
    xdotool key --window "$WIN" --clearmodifiers "$SLOT" # save slot
    sleep 0.6
    xdotool key --window "$WIN" --clearmodifiers y      # confirm yn_dial_box
}
i=0
while [ $i -lt 3 ] && ! load_seen && kill -0 "$HOST_PID" 2>/dev/null; do
    try_load
    j=0
    while [ $j -lt 20 ] && ! load_seen && kill -0 "$HOST_PID" 2>/dev/null; do
        sleep 0.25
        j=$((j + 1))
    done
    i=$((i + 1))
done
sleep 0.5

if [ "$EXPECT" = "crash" ]; then
    i=0
    while [ $i -lt 25 ] && kill -0 "$HOST_PID" 2>/dev/null; do
        sleep 0.2
        i=$((i + 1))
    done
    if kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: expected loader crash, but game is alive" >&2
        cat "$TMP/save.log" >&2 || true
        exit 1
    fi
    echo "PASS: loader crashed as expected on $SAVE_NAME"
else
    if ! kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: loader/game died on $SAVE_NAME" >&2
        cat "$TMP/save.log" >&2 || true
        exit 1
    fi
    if [ -n "$REQUIRE_LOG" ] && ! grep -Fq "$REQUIRE_LOG" "$TMP/save.log"; then
        echo "FAIL: save log did not contain: $REQUIRE_LOG" >&2
        cat "$TMP/save.log" >&2 || true
        exit 1
    fi
    if [ -n "$FORBID_LOG" ] && grep -Fq "$FORBID_LOG" "$TMP/save.log"; then
        echo "FAIL: save log unexpectedly contained: $FORBID_LOG" >&2
        cat "$TMP/save.log" >&2 || true
        exit 1
    fi
    echo "PASS: game alive after loading $SAVE_NAME"
fi
