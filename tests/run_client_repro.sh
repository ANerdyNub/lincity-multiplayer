#!/bin/sh
# Start a minimal fake host, connect the real client, send action lines, and
# check whether the client survived.
#
# Usage:
#   DISPLAY=:99 tests/run_client_repro.sh \
#       [--binary ./xlincity] \
#       [--payload '66 100000 0 0']... [--expect alive|crash] \
#       [--bad-map] [--partial] [--version-mismatch] [--wait-after N]
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_BIN:-$ROOT/xlincity}
DISPLAY=${DISPLAY:-:99}
EXPECT=alive
EXPECT_MAP=ok
HOST_WAIT=4
POST_WAIT=1
PARTIAL=0
VERSION_MISMATCH=0
REQUIRE_LOG=none
EXPECT_REJECTS=

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --display) DISPLAY=$2; shift 2 ;;
    --payload) PAYLOADS="${PAYLOADS:-}${PAYLOADS:+
}$2"; shift 2 ;;
    --expect) EXPECT=$2; shift 2 ;;
    --bad-map) EXPECT_MAP=fail; shift ;;
    --version-mismatch) EXPECT_MAP=version; VERSION_MISMATCH=1; shift ;;
    --partial) PARTIAL=1; HOST_WAIT=8; POST_WAIT=7; shift ;;
    --wait-after) POST_WAIT=$2; shift 2 ;;
    --require-log) REQUIRE_LOG=$2; shift 2 ;;
    --expect-rejects) EXPECT_REJECTS=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN] [--display :N] [--payload STR]... [--expect alive|crash] [--bad-map] [--partial] [--wait-after N] [--require-log none|reject] [--expect-rejects N]" >&2; exit 2 ;;
    esac
done

case "$EXPECT" in alive|crash) ;; *) echo "--expect must be alive or crash" >&2; exit 2 ;; esac
case "$EXPECT_MAP" in ok|fail|version) ;; *) echo "bad map expectation" >&2; exit 2 ;; esac
case "$REQUIRE_LOG" in none|reject) ;; *) echo "--require-log must be none or reject" >&2; exit 2 ;; esac
case "${EXPECT_REJECTS:-0}" in ''|*[!0-9]*) echo "--expect-rejects must be a non-negative integer" >&2; exit 2 ;; esac
[ -x "$BIN" ] || { echo "binary not executable: $BIN" >&2; exit 2; }
command -v xdotool >/dev/null 2>&1 || { echo "xdotool not found" >&2; exit 2; }
DISPLAY=$DISPLAY xdpyinfo >/dev/null 2>&1 || { echo "no X display at $DISPLAY" >&2; exit 2; }

TMP=$(mktemp -d)
CLIENT_PID=
HOST_PID=
cleanup() {
    [ -n "${CLIENT_PID:-}" ] && kill "$CLIENT_PID" 2>/dev/null || true
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null || true
    if [ "${KEEP_TMP:-0}" = 1 ]; then
        echo "kept: $TMP" >&2
    else
        rm -rf "$TMP"
    fi
}
trap cleanup EXIT INT TERM

if [ "$EXPECT_MAP" != ok ] && [ -z "${PAYLOADS:-}" ]; then
    : >"$TMP/payloads"
else
    printf '%s\n' "${PAYLOADS:-66 100000 0 0
68 100000 0 0
70 100000 0 0}" >"$TMP/payloads"
fi

set -- "$ROOT/tests/fake_host.py" --wait "$HOST_WAIT"
[ "$EXPECT_MAP" = fail ] && set -- "$@" --bad-compressed-size
[ "$VERSION_MISMATCH" = 1 ] && set -- "$@" --welcome-version 1 --no-map
[ "$PARTIAL" = 1 ] && set -- "$@" --partial
while IFS= read -r p; do
    set -- "$@" --payload "$p"
done <"$TMP/payloads"
python3 "$@" >"$TMP/host.log" 2>&1 &
HOST_PID=$!
sleep 0.5
if ! kill -0 "$HOST_PID" 2>/dev/null; then
    echo "FAIL: fake host did not start" >&2
    cat "$TMP/host.log" >&2 || true
    exit 1
fi

HOME=$TMP LINCITY_HOME=$ROOT DISPLAY=$DISPLAY "$BIN" >"$TMP/client.log" 2>&1 &
CLIENT_PID=$!

WIN=
i=0
while [ $i -lt 60 ]; do
    WIN=$(DISPLAY=$DISPLAY xdotool search --name 'xlincity' 2>/dev/null | tail -1 || true)
    [ -n "$WIN" ] && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.2
    i=$((i + 1))
done
if [ -z "$WIN" ]; then
    echo "FAIL: game window did not appear" >&2
    cat "$TMP/client.log" >&2 || true
    exit 1
fi

DISPLAY=$DISPLAY xdotool windowfocus --sync "$WIN" 2>/dev/null || true
DISPLAY=$DISPLAY xdotool key --window "$WIN" --clearmodifiers Return
sleep 1.0
if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
    echo "FAIL: client died before the network screen" >&2
    cat "$TMP/client.log" >&2 || true
    exit 1
fi

# Test-only: open the network screen, then join 127.0.0.1:PORT.
NETWORK_FLAG_OFFSET=$(nm "$BIN" 2>/dev/null | awk '$3 == "network_flag" { print $1; exit }')
if [ -z "$NETWORK_FLAG_OFFSET" ]; then
    echo "FAIL: network_flag symbol missing" >&2
    exit 1
fi
python3 - "$CLIENT_PID" "$BIN" "$NETWORK_FLAG_OFFSET" <<'PY'
import os, struct, sys
pid, exe, off = int(sys.argv[1]), os.path.realpath(sys.argv[2]), int(sys.argv[3], 16)
base = None
with open("/proc/%d/maps" % pid) as f:
    for line in f:
        parts = line.split()
        if len(parts) >= 6 and parts[5] == exe and parts[2] == "00000000":
            base = int(parts[0].split("-")[0], 16)
            break
if base is None:
    raise SystemExit("could not find PIE load base")
with open("/proc/%d/mem" % pid, "r+b", 0) as f:
    os.pwrite(f.fileno(), struct.pack("<i", 1), base + off)
PY
sleep 0.7

send_key() {
    if ! DISPLAY=$DISPLAY xdotool key --window "$WIN" --clearmodifiers "$1" 2>"$TMP/xdotool.log"; then
        if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
            echo "FAIL: client died before key '$1'" >&2
            cat "$TMP/client.log" >&2 || true
            exit 1
        fi
        WIN=$(DISPLAY=$DISPLAY xdotool search --name 'xlincity' 2>/dev/null | tail -1 || true)
        [ -n "$WIN" ] && DISPLAY=$DISPLAY xdotool key --window "$WIN" --clearmodifiers "$1" || true
    fi
}

send_key 2       # Join a game
sleep 0.5
send_key Return  # default host 127.0.0.1
sleep 0.5
send_key Return  # default port 5123
sleep 2.0

PAYLOAD_DISPLAY=$(tr '\n' ';' <"$TMP/payloads")
PAYLOAD_COUNT=$(wc -l <"$TMP/payloads" | tr -d ' ')
WAIT=0
while [ "$(grep -c '^sending: ' "$TMP/host.log" 2>/dev/null || true)" -lt "$PAYLOAD_COUNT" ]; do
    kill -0 "$HOST_PID" 2>/dev/null || break
    sleep 0.1
    WAIT=$((WAIT + 1))
    [ "$WAIT" -ge 100 ] && break
done
if [ "$(grep -c '^sending: ' "$TMP/host.log" 2>/dev/null || true)" -lt "$PAYLOAD_COUNT" ]; then
    echo "FAIL: host did not send all payloads (payloads='$PAYLOAD_DISPLAY')" >&2
    cat "$TMP/host.log" >&2 || true
    cat "$TMP/client.log" >&2 || true
    exit 1
fi
if [ "$EXPECT_MAP" = ok ] && ! grep -qF 'network: recv_map: done OK' "$TMP/client.log"; then
    echo "FAIL: client did not receive the map (payloads='$PAYLOAD_DISPLAY')" >&2
    cat "$TMP/client.log" >&2 || true
    cat "$TMP/host.log" >&2 || true
    exit 1
fi
if [ "$EXPECT_MAP" = fail ]; then
    if grep -qF 'network: recv_map: done OK' "$TMP/client.log"; then
        echo "FAIL: client accepted the malformed map header" >&2
        cat "$TMP/client.log" >&2 || true
        exit 1
    fi
    if ! grep -qF 'network: compressed world length out of range' "$TMP/client.log"; then
        echo "FAIL: client did not reject the compressed size" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
fi
if [ "$EXPECT_MAP" = version ]; then
    if grep -qF 'network: recv_map: done OK' "$TMP/client.log"; then
        echo "FAIL: client accepted a mismatched protocol version" >&2
        cat "$TMP/client.log" >&2 || true
        exit 1
    fi
    if ! grep -qF 'network: recv_map: protocol version mismatch' "$TMP/client.log"; then
        echo "FAIL: client did not reject the welcome version" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
fi
if grep -qF 'fake host error' "$TMP/host.log"; then
    echo "FAIL: fake host lost the connection (payloads='$PAYLOAD_DISPLAY')" >&2
    cat "$TMP/host.log" >&2 || true
    cat "$TMP/client.log" >&2 || true
    exit 1
fi
if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
    echo "FAIL: client died after host payloads='$PAYLOAD_DISPLAY'" >&2
    cat "$TMP/client.log" >&2 || true
    cat "$TMP/host.log" >&2 || true
    exit 1
fi

REJECTS=0
if [ "$REQUIRE_LOG" = reject ]; then
    REJECTS=$(grep -cF 'network: rejected' "$TMP/client.log" 2>/dev/null || true)
    if [ -z "${EXPECT_REJECTS:-}" ]; then
        EXPECT_REJECTS=$PAYLOAD_COUNT
    fi
    if [ "$REJECTS" -ne "$EXPECT_REJECTS" ]; then
        echo "FAIL: expected $EXPECT_REJECTS client reject log lines, got $REJECTS (payloads='$PAYLOAD_DISPLAY')" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
fi

if [ "$EXPECT" = "crash" ]; then
    i=0
    while [ $i -lt 25 ] && kill -0 "$CLIENT_PID" 2>/dev/null; do
        sleep 0.2
        i=$((i + 1))
    done
    if kill -0 "$CLIENT_PID" 2>/dev/null; then
        echo "FAIL: expected client crash, but client is alive (payloads='$PAYLOAD_DISPLAY')" >&2
        cat "$TMP/client.log" >&2 || true
        exit 1
    fi
    echo "PASS: client crashed as expected (payloads='$PAYLOAD_DISPLAY')"
else
    sleep "$POST_WAIT"
    if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
        echo "FAIL: client died after host payloads='$PAYLOAD_DISPLAY'" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if [ "$PARTIAL" = 1 ] && ! grep -qF 'network: line read timed out' "$TMP/client.log"; then
        echo "FAIL: partial action line did not time out" >&2
        cat "$TMP/client.log" >&2 || true
        exit 1
    fi
    echo "PASS: client alive after host payloads='$PAYLOAD_DISPLAY' (rejects=$REJECTS)"
fi
