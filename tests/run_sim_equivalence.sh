#!/bin/sh
# Compare simulation output from a baseline build with the current build.
#
# The baseline binary must be built from the same save format.  Both runs use
# the same explicit --seed, so any difference is a simulation difference rather
# than libc's default random state.  Outputs must be byte-identical; on failure
# the checker prints the documented save-field differences to help localize it.
#
# usage: tests/run_sim_equivalence.sh --baseline BIN [--current BIN]
#        [--ticks "0 1 3600"]
#
# --baseline/--reference and --current/--binary are aliases.  The baseline may
# also be supplied through LINCITY_REF_BIN.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REF=${LINCITY_REF_BIN:-}
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
CORPUS=$ROOT/tests/fuzz_save_corpus
TICKS="0 1 3600"

while [ $# -gt 0 ]; do
    case "$1" in
    --baseline|--reference) REF=$2; shift 2 ;;
    --current|--binary) BIN=$2; shift 2 ;;
    --ticks) TICKS=$2; shift 2 ;;
    --corpus) CORPUS=$2; shift 2 ;;
    *) echo "usage: $0 --baseline BIN [--current BIN] [--ticks LIST] [--corpus DIR]" >&2; exit 2 ;;
    esac
done

[ -n "$REF" ] || { echo "run_sim_equivalence: --baseline BIN is required" >&2; exit 2; }
[ -x "$REF" ] || { echo "not executable: $REF" >&2; exit 2; }
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 2; }

case "$REF" in
    /*) ;;
    *) REF=$(command -v "$REF" 2>/dev/null || printf '%s/%s' "$(pwd)" "$REF") ;;
esac
case "$BIN" in
    /*) ;;
    *) BIN=$(command -v "$BIN" 2>/dev/null || printf '%s/%s' "$ROOT" "$BIN") ;;
esac

REF_HOME=$(CDPATH= cd -- "$(dirname -- "$REF")" && pwd)
NEW_HOME=$ROOT

# The all-groups fixture is the only input that reaches every base dispatch
# case; include the corpus and both opening saves for broad state coverage.
FIXTURES="all_groups opening_good_times opening_bad_times"
for name in good_times bad_times normal_a normal_d; do
    if [ -f "$CORPUS/$name.scn.gz" ] || [ -f "$CORPUS/$name.scn" ]; then
        FIXTURES="$FIXTURES $name"
    fi
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home"

failures=0
runs=0

fixture_path ()
{
    case "$1" in
    all_groups) printf '%s/tests/fixtures/all_groups.scn.gz' "$ROOT" ;;
    opening_good_times) printf '%s/opening/good_times.scn' "$ROOT" ;;
    opening_bad_times) printf '%s/opening/bad_times.scn' "$ROOT" ;;
    *)
        if [ -f "$CORPUS/$1.scn.gz" ]; then
            printf '%s' "$CORPUS/$1.scn.gz"
        else
            printf '%s/%s.scn' "$CORPUS" "$1"
        fi
        ;;
    esac
}

for ticks in $TICKS; do
    for name in $FIXTURES; do
        fixture=$(fixture_path "$name")
        if [ ! -f "$fixture" ]; then
            echo "FAIL: missing fixture for $name: $fixture" >&2
            failures=$((failures + 1))
            continue
        fi

        runs=$((runs + 1))
        ref_out=$TMP/$name.$ticks.ref
        new_out=$TMP/$name.$ticks.new

        if ! env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$REF_HOME" \
            "$REF" --load "$fixture" --save "$ref_out" \
            --ticks "$ticks" --seed 1 >"$TMP/$name.$ticks.ref.log" 2>&1; then
            echo "FAIL: baseline run failed: $name ticks=$ticks" >&2
            cat "$TMP/$name.$ticks.ref.log" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$NEW_HOME" \
            "$BIN" --load "$fixture" --save "$new_out" \
            --ticks "$ticks" --seed 1 >"$TMP/$name.$ticks.new.log" 2>&1; then
            echo "FAIL: current run failed: $name ticks=$ticks" >&2
            cat "$TMP/$name.$ticks.new.log" >&2
            failures=$((failures + 1))
            continue
        fi

        if ! cmp -s "$ref_out" "$new_out"; then
            echo "FAIL: output differs: $name ticks=$ticks" >&2
            python3 "$ROOT/tests/save_roundtrip.py" \
                "$ref_out" "$new_out" >&2 || true
            failures=$((failures + 1))
            continue
        fi

        echo "PASS: $name ticks=$ticks"
    done
done

if [ "$failures" -ne 0 ]; then
    echo "FAIL: $failures simulation comparison(s) differed" >&2
    exit 1
fi

echo "PASS: $runs simulation runs match the baseline byte for byte"
