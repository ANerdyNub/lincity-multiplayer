#!/bin/sh
# Start an in-game host, connect protocol clients, and check whether the host
# survived and whether the expected broadcasts/actions occurred.
#
# Usage:
#   DISPLAY=:99 tests/run_network_repro.sh \
#       [--payload '66 100000 0 0']... [--expect alive|crash] \
#       [--require-log any|reject|accept|overlong] [--require-line TEXT] \
#       [--expect-build X,Y,TYPE]... [--expect-bulldoze X,Y]... \
#       [--expect-mflags X,Y,FLAGS]... \
#       [--binary ./xlincity] [--origin 74] [--seed 2] \
#       [--hello-version N] [--expect-hello-rejected] [--no-hello] \
#       [--slowline|--never-read|--overlong|--cost]
#
# Requires an X display (Xephyr is fine), but no window manager, xdotool,
# /proc/<pid>/mem poke, or lobby keystrokes.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_BIN:-$ROOT/xlincity}
DISPLAY=${DISPLAY:-:99}
EXPECT="alive"
REQUIRE_LOG="any"
REQUIRE_LINE=
ORIGIN=
SEED=
HELLO_VERSION=2
EXPECT_HELLO_REJECTED=0
NO_HELLO=0
SLOWLINE=0
NEVER_READ=0
OVERLONG=0
COST=0
NEVER_READ_DURATION=30
HOST_START_ATTEMPTS=${HOST_START_ATTEMPTS:-2}

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --display) DISPLAY=$2; shift 2 ;;
    --payload) PAYLOADS="${PAYLOADS:-}${PAYLOADS:+
}$2"; shift 2 ;;
    --expect) EXPECT=$2; shift 2 ;;
    --require-log) REQUIRE_LOG=$2; shift 2 ;;
    --require-line) REQUIRE_LINE=$2; shift 2 ;;
    --expect-build) EXPECT_BUILDS="${EXPECT_BUILDS:-}${EXPECT_BUILDS:+
}$2"; shift 2 ;;
    --expect-bulldoze) EXPECT_BULLDOZES="${EXPECT_BULLDOZES:-}${EXPECT_BULLDOZES:+
}$2"; shift 2 ;;
    --expect-mflags) EXPECT_MFLAGS="${EXPECT_MFLAGS:-}${EXPECT_MFLAGS:+
}$2"; shift 2 ;;
    --origin) ORIGIN=$2; shift 2 ;;
    --seed) SEED=$2; shift 2 ;;
    --hello-version) HELLO_VERSION=$2; shift 2 ;;
    --expect-hello-rejected) EXPECT_HELLO_REJECTED=1; shift ;;
    --no-hello) NO_HELLO=1; shift ;;
    --slowline) SLOWLINE=1; shift ;;
    --never-read) NEVER_READ=1; shift ;;
    --never-read-duration) NEVER_READ_DURATION=$2; shift 2 ;;
    --overlong) OVERLONG=1; shift ;;
    --cost) COST=1; shift ;;
    *) echo "usage: $0 [--binary BIN] [--display :N] [--payload STR]... [--expect alive|crash] [--require-log any|reject|accept|overlong] [--require-line TEXT] [--expect-build X,Y,TYPE]... [--expect-bulldoze X,Y]... [--expect-mflags X,Y,FLAGS]... [--origin N] [--seed N] [--slowline|--never-read|--overlong|--cost] [--never-read-duration N]" >&2; exit 2 ;;
    esac
done

case "$EXPECT" in alive|crash) ;; *) echo "--expect must be alive or crash" >&2; exit 2 ;; esac
case "$REQUIRE_LOG" in any|reject|accept|overlong) ;; *) echo "--require-log must be any, reject, accept, or overlong" >&2; exit 2 ;; esac
case "${ORIGIN:-}" in
    "") ;;
    *[!0-9]*) echo "--origin must be a non-negative integer" >&2; exit 2 ;;
esac
case "${SEED:-}" in
    "") ;;
    *[!0-9]*) echo "--seed must be a non-negative integer" >&2; exit 2 ;;
esac
case "$HELLO_VERSION" in
    ''|*[!0-9]*) echo "--hello-version must be a non-negative integer" >&2; exit 2 ;;
esac
[ -x "$BIN" ] || { echo "binary not executable: $BIN" >&2; exit 2; }
DISPLAY=$DISPLAY xdpyinfo >/dev/null 2>&1 || { echo "no X display at $DISPLAY" >&2; exit 2; }

TMP=$(mktemp -d)
HOST_PID=
CLIENT_PID=
SECOND_PID=
cleanup() {
    [ -n "${SECOND_PID:-}" ] && kill "$SECOND_PID" 2>/dev/null || true
    [ -n "${CLIENT_PID:-}" ] && kill "$CLIENT_PID" 2>/dev/null || true
    [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null || true
    if [ "${KEEP_TMP:-0}" = 1 ]; then
        echo "kept: $TMP" >&2
    else
        rm -rf "$TMP"
    fi
}
trap cleanup EXIT INT TERM

printf '%s\n' "${PAYLOADS:-66 100000 0 0}" >"$TMP/payloads"

PLAYERS=1
[ "$NEVER_READ" = 1 ] && PLAYERS=2

set -- --host "$PLAYERS"
[ -n "${ORIGIN:-}" ] && set -- "$@" --origin "$ORIGIN"
[ -n "${SEED:-}" ] && set -- "$@" --seed "$SEED"

start_host() {
    HOME=$TMP LINCITY_HOME=$ROOT "$BIN" "$@" >"$TMP/host.log" 2>&1 &
    HOST_PID=$!

    # The listener is created before the host waits for clients.  Waiting for
    # this line avoids probing the port (which would consume a client slot).
    i=0
    while [ $i -lt 100 ]; do
        if grep -Fq 'network: host_start OK' "$TMP/host.log"; then
            return 0
        fi
        if ! kill -0 "$HOST_PID" 2>/dev/null; then
            wait "$HOST_PID" 2>/dev/null || true
            HOST_PID=
            return 1
        fi
        sleep 0.2
        i=$((i + 1))
    done
    kill "$HOST_PID" 2>/dev/null || true
    wait "$HOST_PID" 2>/dev/null || true
    HOST_PID=
    return 1
}

attempt=1
while ! start_host "$@"; do
    if [ "$attempt" -ge "$HOST_START_ATTEMPTS" ]; then
        echo "FAIL: host did not start listening after $attempt attempt(s)" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "WARNING: headless host start attempt $attempt failed; retrying once (Xephyr/X11 startup)" >&2
    attempt=$((attempt + 1))
    sleep 0.5
done

if [ "$NEVER_READ" = 1 ]; then
    python3 "$ROOT/tests/never_read_client.py" --host 127.0.0.1 --port 5123 \
        --duration "$NEVER_READ_DURATION" >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
    python3 "$ROOT/tests/normal_client.py" --host 127.0.0.1 --port 5123 \
        --duration "$((NEVER_READ_DURATION + 10))" >"$TMP/normal.log" 2>&1 &
    SECOND_PID=$!
elif [ "$OVERLONG" = 1 ]; then
    python3 "$ROOT/tests/overlong_client.py" --host 127.0.0.1 --port 5123 \
        >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
elif [ "$COST" = 1 ]; then
    python3 "$ROOT/tests/cost_client.py" --host 127.0.0.1 --port 5123 \
        >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
elif [ "$SLOWLINE" = 1 ]; then
    python3 "$ROOT/tests/slow_line_client.py" --host 127.0.0.1 --port 5123 \
        --interval 4 --duration 30 >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
elif [ "$NO_HELLO" = 1 ]; then
    python3 "$ROOT/tests/silent_client.py" --host 127.0.0.1 --port 5123 \
        --duration 10 >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
    sleep 1
    python3 "$ROOT/tests/fake_client.py" --host 127.0.0.1 --port 5123 \
        --wait 1 >"$TMP/normal.log" 2>&1 &
    SECOND_PID=$!
else
    set -- "$ROOT/tests/fake_client.py" --wait 1 \
        --hello-version "$HELLO_VERSION"
    [ "$EXPECT_HELLO_REJECTED" = 1 ] && set -- "$@" --expect-disconnect
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        set -- "$@" --payload "$p"
    done <"$TMP/payloads"
    if [ -n "${EXPECT_BUILDS:-}" ]; then
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            set -- "$@" --expect-build "$p"
        done <<EOF
$EXPECT_BUILDS
EOF
    fi
    if [ -n "${EXPECT_BULLDOZES:-}" ]; then
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            set -- "$@" --expect-bulldoze "$p"
        done <<EOF
$EXPECT_BULLDOZES
EOF
    fi
    if [ -n "${EXPECT_MFLAGS:-}" ]; then
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            set -- "$@" --expect-mflags "$p"
        done <<EOF
$EXPECT_MFLAGS
EOF
    fi
    python3 "$@" >"$TMP/client.log" 2>&1 &
    CLIENT_PID=$!
fi

# The host sends the map once the fixed player count is connected.  There is
# no lobby key to send and therefore no in-game `s` save dialog to open.
sleep 0.5
if ! kill -0 "$HOST_PID" 2>/dev/null; then
    echo "host died after client connect (before checks)" >&2
fi

CLIENT_RC=0
wait "$CLIENT_PID" || CLIENT_RC=$?
CLIENT_PID=
SECOND_RC=0
if [ -n "${SECOND_PID:-}" ]; then
    wait "$SECOND_PID" || SECOND_RC=$?
    SECOND_PID=
fi

if [ "$CLIENT_RC" -ne 0 ]; then
    echo "FAIL: fake client exited $CLIENT_RC" >&2
    cat "$TMP/client.log" >&2 || true
    cat "$TMP/normal.log" >&2 || true
    cat "$TMP/host.log" >&2 || true
    exit 1
fi
if [ "$SECOND_RC" -ne 0 ]; then
    echo "FAIL: second normal client exited $SECOND_RC" >&2
    cat "$TMP/normal.log" >&2 || true
    cat "$TMP/host.log" >&2 || true
    exit 1
fi

PAYLOAD_DISPLAY=$(tr '\n' ';' <"$TMP/payloads")

if [ "$NO_HELLO" = 1 ]; then
    if ! kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: host died after the silent client" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if ! grep -Fq 'network: rejected client protocol hello (no line)' "$TMP/host.log"; then
        echo "FAIL: host did not time out the silent client" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    accepted=$(grep -c 'network: headless host accepted fd=' "$TMP/host.log" || true)
    if [ "$accepted" -ne 1 ]; then
        echo "FAIL: silent client consumed a host slot (accepted=$accepted)" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if ! grep -Fq 'network: headless host game started with 1 client(s)' "$TMP/host.log"; then
        echo "FAIL: valid client did not become the only host client" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "PASS: silent client timed out without holding a host slot"
    exit 0
fi

if [ "$COST" = 1 ]; then
    if ! kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: host died during the cost test" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if ! grep -Fq 'network: rejected MSG_BUILD placement (4,4,-1)' "$TMP/host.log"; then
        echo "FAIL: no-credit parkland build was not rejected" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "PASS: cost authority: parkland rejected, market build and bulldoze charged"
    exit 0
fi

if [ "$EXPECT_HELLO_REJECTED" = 1 ]; then
    if ! kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: host died after rejecting the protocol hello" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if ! grep -Fq "network: rejected client protocol version $HELLO_VERSION (expected 2)" "$TMP/host.log"; then
        echo "FAIL: host did not reject protocol version $HELLO_VERSION" >&2
        cat "$TMP/client.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "PASS: host rejected protocol version $HELLO_VERSION before a map"
    exit 0
fi

if [ "$EXPECT" = "crash" ]; then
    i=0
    while [ $i -lt 25 ] && kill -0 "$HOST_PID" 2>/dev/null; do
        sleep 0.2
        i=$((i + 1))
    done
    if kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: expected host crash, but host is alive (payloads='$PAYLOAD_DISPLAY')" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "PASS: host crashed as expected (payloads='$PAYLOAD_DISPLAY')"
else
    sleep 1
    if ! kill -0 "$HOST_PID" 2>/dev/null; then
        echo "FAIL: host died after payloads='$PAYLOAD_DISPLAY'" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if [ -n "${REQUIRE_LINE:-}" ] && ! grep -Fq "$REQUIRE_LINE" "$TMP/host.log"; then
        echo "FAIL: expected host log line '$REQUIRE_LINE' (payloads='$PAYLOAD_DISPLAY')" >&2
        [ -f "$TMP/normal.log" ] && cat "$TMP/normal.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if [ "$REQUIRE_LOG" = "reject" ] && ! grep -Fq 'network: rejected' "$TMP/host.log"; then
        echo "FAIL: expected a rejection log (payloads='$PAYLOAD_DISPLAY')" >&2
        [ -f "$TMP/normal.log" ] && cat "$TMP/normal.log" >&2 || true
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if [ "$REQUIRE_LOG" = "overlong" ] && ! grep -Fq 'network: host line too long; disconnecting client' "$TMP/host.log"; then
        echo "FAIL: expected the overlong-line disconnect log" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    if [ "$REQUIRE_LOG" = "accept" ] && grep -q 'network: rejected' "$TMP/host.log"; then
        echo "FAIL: expected no rejection log (payloads='$PAYLOAD_DISPLAY')" >&2
        cat "$TMP/host.log" >&2 || true
        exit 1
    fi
    echo "PASS: host alive after payloads='$PAYLOAD_DISPLAY' (log=$REQUIRE_LOG)"
fi
