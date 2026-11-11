#!/usr/bin/env bash
# Run the complete host/client network matrix.  An X display is required;
# CI wraps this in xvfb-run, while a desktop can use its existing DISPLAY.
#
# Usage:
#   tests/run_network_matrix.sh [--binary ./xlincity] [--display :99]
#                               [--log-dir DIR] [--seed N]
#
# Sanitizer reports and per-case command logs are written under --log-dir.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_BIN:-$ROOT/xlincity}
DISPLAY=${DISPLAY:-:99}
LOG_DIR=${MATRIX_LOG_DIR:-$(mktemp -d)}
SEED=2

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --display) DISPLAY=$2; shift 2 ;;
    --log-dir) LOG_DIR=$2; shift 2 ;;
    --seed) SEED=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN] [--display :N] [--log-dir DIR] [--seed N]" >&2; exit 2 ;;
    esac
done

case "$SEED" in
    ''|*[!0-9]*) echo "--seed must be a non-negative integer" >&2; exit 2 ;;
esac
[ -x "$BIN" ] || { echo "binary not executable: $BIN" >&2; exit 2; }
command -v xdpyinfo >/dev/null 2>&1 || { echo "xdpyinfo not found" >&2; exit 2; }
DISPLAY=$DISPLAY xdpyinfo >/dev/null 2>&1 || { echo "no X display at $DISPLAY" >&2; exit 2; }

mkdir -p "$LOG_DIR"
LOG_DIR=$(CDPATH= cd -- "$LOG_DIR" && pwd)
REPORT_DIR=$LOG_DIR/sanitizer
rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR"

export DISPLAY
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:halt_on_error=1:log_path=$REPORT_DIR/asan"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:log_path=$REPORT_DIR/ubsan"

PASS=0
FAIL=0
FAILED=()

count_reports() {
    find "$REPORT_DIR" -type f 2>/dev/null | wc -l | tr -d ' '
}

run_case() {
    name=$1
    shift
    before=$(count_reports)
    log=$LOG_DIR/matrix-$name.log
    timeout 240 "$@" >"$log" 2>&1
    rc=$?
    after=$(count_reports)
    if [ "$rc" -eq 0 ] && [ "$after" -eq "$before" ]; then
        echo "PASS $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL $name (rc=$rc, new sanitizer reports=$((after - before)))"
        tail -60 "$log" | sed 's/^/    /'
        FAIL=$((FAIL + 1))
        FAILED+=("$name")
    fi
}

# Every host case uses the same fixed seed.  Seed 2 has a known live port
# bank at (37,76), outside the random starting village.
run_host_case() {
    local name=$1
    shift
    local args=(--binary "$BIN" --display "$DISPLAY" --expect alive --seed "$SEED")
    while [ $# -gt 0 ]; do
        args+=("$1" "$2")
        shift 2
    done
    run_case "$name" "$ROOT/tests/run_network_repro.sh" "${args[@]}"
}

run_client() {
    local name=$1
    shift
    run_case "$name" "$ROOT/tests/run_client_repro.sh" \
        --binary "$BIN" --display "$DISPLAY" --expect alive "$@"
}

echo "== ASan+UBSan host edge matrix =="
run_host_case c1 \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 100000 0 0'
run_host_case c2-bulldoze \
    --require-log reject --require-line 'network: rejected MSG_BULLDOZE position' \
    --payload '68 100000 0 0'
run_host_case c2-mflags \
    --require-log reject --require-line 'network: rejected MSG_MFLAGS position' \
    --payload '70 100000 0 0'
run_host_case negative-coords \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 -1 0 0' --payload '68 -1 0 0' --payload '70 -1 0 0'
run_host_case size1-past-edge \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 100 0 80'
run_host_case size2-past-edge \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 99 0 71'
run_host_case size3-past-edge \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 98 0 61'
run_host_case size4-past-edge \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 97 0 332'
run_host_case corner \
    --require-log reject --require-line 'network: rejected MSG_BUILD position' \
    --payload '66 99 99 71' --payload '68 99 99 0' --payload '70 99 99 0'
run_host_case outer-ring \
    --require-log accept \
    --payload '68 0 0 0' --payload '66 0 0 80' \
    --payload '68 0 1 0' --payload '66 0 1 130' \
    --payload '68 0 2 0' --payload '66 0 2 110' \
    --payload '68 0 3 0' --payload '66 0 3 1' \
    --payload '68 99 0 0' --payload '66 99 0 80' \
    --payload '68 99 1 0' --payload '66 99 1 130' \
    --payload '68 99 2 0' --payload '66 99 2 110' \
    --payload '68 99 3 0' --payload '66 99 3 1' \
    --payload '68 4 0 0' --payload '66 4 0 80' \
    --payload '68 5 0 0' --payload '66 5 0 130' \
    --payload '68 6 0 0' --payload '66 6 0 110' \
    --payload '68 7 0 0' --payload '66 7 0 1' \
    --payload '68 4 99 0' --payload '66 4 99 80' \
    --payload '68 5 99 0' --payload '66 5 99 130' \
    --payload '68 6 99 0' --payload '66 6 99 110' \
    --payload '68 7 99 0' --payload '66 7 99 1' \
    --expect-build '0,0,80' --expect-build '0,1,130' \
    --expect-build '0,2,110' --expect-build '0,3,1' \
    --expect-build '99,0,80' --expect-build '99,1,130' \
    --expect-build '99,2,110' --expect-build '99,3,1' \
    --expect-build '4,0,80' --expect-build '5,0,130' \
    --expect-build '6,0,110' --expect-build '7,0,1' \
    --expect-build '4,99,80' --expect-build '5,99,130' \
    --expect-build '6,99,110' --expect-build '7,99,1'
run_host_case near-edge-origin74 \
    --origin 74 --require-log accept \
    --payload '68 98 10 0' --payload '66 98 10 80' \
    --payload '68 10 98 0' --payload '66 10 98 130' \
    --payload '68 98 98 0' --payload '66 98 98 110' \
    --payload '68 97 97 0' --payload '66 97 97 1' \
    --expect-build '98,10,80' --expect-build '10,98,130' \
    --expect-build '98,98,110' --expect-build '97,97,1'
run_host_case type-max \
    --require-log reject --require-line 'network: rejected MSG_BUILD type' \
    --payload '66 10 10 400'
run_host_case type-negative \
    --require-log reject --require-line 'network: rejected MSG_BUILD type' \
    --payload '66 10 10 -1'
run_host_case huge-coord \
    --require-log reject --require-line 'network: rejected malformed action line' \
    --payload '66 999999999999999999999 0 0'
run_host_case huge-flag \
    --require-log reject --require-line 'network: rejected malformed action line' \
    --payload '70 0 0 999999999999999999999'
run_host_case mflags-wrong-tile \
    --require-log reject --require-line 'network: rejected MSG_MFLAGS group' \
    --payload '70 0 0 256'
run_host_case mflags-full-market \
    --require-log accept \
    --payload '66 2 2 71' --payload '70 2 2 1048320' \
    --expect-build '2,2,71' --expect-mflags '2,2,1048320'
run_host_case mflags-full-plus-coverage \
    --require-log accept \
    --payload '66 2 2 71' --payload '70 2 2 7372544' \
    --expect-build '2,2,71' --expect-mflags '2,2,32512'
run_host_case mflags-single-trade \
    --require-log accept \
    --payload '66 2 2 71' --payload '70 2 2 256' \
    --expect-build '2,2,71' --expect-mflags '2,2,256'
run_host_case mflags-port \
    --require-log accept \
    --payload '66 37 76 161' --payload '70 37 76 7372544' \
    --expect-build '37,76,161' --expect-mflags '37,76,32512'
run_host_case build-occupied \
    --require-log reject --require-line 'network: rejected MSG_BUILD placement' \
    --payload '66 2 2 71' --payload '66 2 2 71' \
    --expect-build '2,2,71'
run_host_case build-market-subtile \
    --require-log reject --require-line 'network: rejected MSG_BUILD placement' \
    --payload '66 2 2 71' --payload '66 3 3 71' \
    --expect-build '2,2,71'
run_host_case valid-build-bulldoze \
    --require-log accept \
    --payload '66 2 2 71' --payload '68 2 2 0' \
    --expect-build '2,2,71' --expect-bulldoze '2,2'

echo "== ASan+UBSan host special cases =="
run_case slowline "$ROOT/tests/run_network_repro.sh" \
    --binary "$BIN" --display "$DISPLAY" --seed "$SEED" --slowline --expect alive
run_case never-read "$ROOT/tests/run_network_repro.sh" \
    --binary "$BIN" --display "$DISPLAY" --seed "$SEED" --never-read \
    --never-read-duration 30 --expect alive --require-log reject \
    --require-line 'network: rejected malformed action line'
run_case overlong "$ROOT/tests/run_network_repro.sh" \
    --binary "$BIN" --display "$DISPLAY" --seed "$SEED" --overlong \
    --expect alive --require-log overlong
run_host_case origin74-recheck \
    --origin 74 --require-log accept \
    --payload '66 98 10 80' --payload '66 10 98 130' \
    --payload '66 98 98 110' --payload '66 97 97 1' \
    --expect-build '98,10,80' --expect-build '10,98,130' \
    --expect-build '98,98,110' --expect-build '97,97,1'

echo "== ASan+UBSan client cases =="
run_client p6 \
    --payload '66 100000 0 0' --payload '68 100000 0 0' \
    --payload '70 100000 0 0' --require-log reject --expect-rejects 3
run_client client-edges \
    --payload '66 -1 0 0' --payload '68 -1 0 0' --payload '70 -1 0 0' \
    --payload '66 100 0 80' --payload '66 99 0 71' \
    --payload '66 98 0 61' --payload '66 97 0 332' \
    --payload '66 99 99 71' --payload '68 99 99 0' --payload '70 99 99 0' \
    --payload '66 10 10 400' --payload '66 10 10 -1' \
    --payload '66 999999999999999999999 0 0' \
    --payload '68 999999999999999999999 0 0' \
    --payload '70 0 0 999999999999999999999' \
    --payload '66 10 10 999999999999999999999' \
    --require-log reject --expect-rejects 15
run_client bad-map --bad-map
run_client partial-line --partial --payload '66 1 1 71'
run_client valid-map --payload '66 2 2 71'

echo "== summary: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then
    printf 'failed: %s\n' "${FAILED[*]}"
fi
reports=$(find "$REPORT_DIR" -type f 2>/dev/null)
if [ -n "$reports" ]; then
    echo "sanitizer report files:"
    printf '%s\n' "$reports"
else
    echo "sanitizer report files: none"
fi
echo "matrix logs: $LOG_DIR"
[ "$FAIL" -eq 0 ] && [ -z "$reports" ]
