#!/bin/sh
# Verify the all-groups fixture and report which base groups the save corpus
# actually exercises.  Three in-game years is 3 * 12 * 100 ticks.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${LINCITY_ASAN_BIN:-$ROOT/xlincity}
CORPUS=$ROOT/tests/fuzz_save_corpus
FIXTURE=$ROOT/tests/fixtures/all_groups.scn.gz
YEARS=${LINCITY_COVERAGE_YEARS:-3}

while [ $# -gt 0 ]; do
    case "$1" in
    --binary) BIN=$2; shift 2 ;;
    --corpus) CORPUS=$2; shift 2 ;;
    --fixture) FIXTURE=$2; shift 2 ;;
    --years) YEARS=$2; shift 2 ;;
    *) echo "usage: $0 [--binary BIN] [--corpus DIR] [--fixture FILE] [--years N]" >&2; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 2; }
[ -f "$FIXTURE" ] || { echo "missing fixture: $FIXTURE" >&2; exit 2; }
case "$BIN" in
    /*) ;;
    *) BIN=$(command -v "$BIN" 2>/dev/null || printf '%s/%s' "$ROOT" "$BIN") ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/home"
TICKS=$((YEARS * 12 * 100))

run_one() {
    label=$1
    source=$2
    ticks=$3
    env -u DISPLAY HOME="$TMP/home" LINCITY_HOME="$ROOT" \
        "$BIN" --load "$source" --save "$TMP/$label.scn" --ticks "$ticks" \
        >"$TMP/$label.log" 2>&1 || {
        echo "FAIL: headless run failed for $label ($ticks ticks)" >&2
        cat "$TMP/$label.log" >&2
        exit 1
    }
    printf '%s\n' "$TMP/$label.scn"
}

echo "== all-groups fixture (must retain every base group 0..40)"
fixture_zero=$(run_one fixture_0 "$FIXTURE" 0)
fixture_one=$(run_one fixture_1 "$FIXTURE" 1)
python3 "$ROOT/tests/group_coverage.py" --require-all "$FIXTURE" \
    "$fixture_zero" "$fixture_one"

echo
echo "== corpus coverage at 0 and $TICKS ticks ($YEARS in-game years)"
corpus_outputs=
for name in good_times bad_times normal_a normal_d; do
    fixture=$CORPUS/$name.scn.gz
    [ -f "$fixture" ] || fixture=$CORPUS/$name.scn
    [ -f "$fixture" ] || { echo "missing fixture: $CORPUS/$name.scn(.gz)" >&2; exit 1; }
    out=$(run_one "corpus_${name}_0" "$fixture" 0)
    corpus_outputs="$corpus_outputs $out"
    out=$(run_one "corpus_${name}_${TICKS}" "$fixture" "$TICKS")
    corpus_outputs="$corpus_outputs $out"
done
for name in good_times bad_times; do
    fixture=$ROOT/opening/$name.scn
    [ -f "$fixture" ] || { echo "missing fixture: $fixture" >&2; exit 1; }
    out=$(run_one "opening_${name}_0" "$fixture" 0)
    corpus_outputs="$corpus_outputs $out"
    out=$(run_one "opening_${name}_${TICKS}" "$fixture" "$TICKS")
    corpus_outputs="$corpus_outputs $out"
done
# shellcheck disable=SC2086
python3 "$ROOT/tests/group_coverage.py" $corpus_outputs

echo
echo "PASS: all-groups fixture covers 0..40 and corpus report completed"
