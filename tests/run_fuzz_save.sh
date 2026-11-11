#!/bin/sh
# Build and run the save-parser libFuzzer target.
#
#   CC=/path/to/clang tests/run_fuzz_save.sh [seconds]
#   FUZZ_MODE=full CC=/path/to/clang tests/run_fuzz_save.sh [seconds]
#   FUZZ_MODE=mapcells CC=/path/to/clang tests/run_fuzz_save.sh [seconds]
#
# Fast mode derives suffixes from the compressed full saves in
# tests/fuzz_save_corpus and uses a valid map cached once.  Full mode feeds the
# complete text saves through save_read_map() and is intentionally slower.
# Mapcells mode starts from the cached map and mutates a few cells before
# calling map_state_ok().
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-clang}
SECONDS=${1:-900}
BIN=${FUZZ_BIN:-/tmp/fuzz_save_parser}
CORPUS=${FUZZ_CORPUS:-/tmp/fuzz-save-corpus}
SEEDS=$ROOT/tests/fuzz_save_corpus
MODE=${FUZZ_MODE:-fast}
DICT=$SEEDS/save.dict

command -v "$CC" >/dev/null 2>&1 || {
    echo "clang with libFuzzer support is required (set CC)" >&2
    exit 2
}

SEED_TMP=$(mktemp -d)
trap 'rm -rf "$SEED_TMP"' EXIT INT TERM
for seed in "$SEEDS"/*.scn.gz; do
    [ -f "$seed" ] || continue
    gzip -dc "$seed" > "$SEED_TMP/$(basename "$seed" .gz)"
done
[ -f "$SEED_TMP/good_times.scn" ] || {
    echo "missing seed $SEEDS/good_times.scn.gz" >&2
    exit 2
}

"$CC" -O1 -g -D_GNU_SOURCE -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
    -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -o "$BIN" "$ROOT/tests/fuzz_save_parser.c"

rm -rf "$CORPUS"
mkdir -p "$CORPUS" /tmp/fuzz-artifacts
case "$MODE" in
fast)
    python3 - "$SEED_TMP" "$CORPUS" <<'PY'
import glob
import os
import sys

src, dst = sys.argv[1], sys.argv[2]
for path in sorted(glob.glob(os.path.join(src, "*.scn"))):
    with open(path, "r") as f:
        lines = f.read().splitlines()
    # version line + 100x100x42 map lines; retain the real suffix.
    if len(lines) <= 420001:
        raise SystemExit("%s is too short" % path)
    name = os.path.basename(path)
    with open(os.path.join(dst, name + ".tail"), "w") as f:
        f.write("\n".join(lines[420001:]) + "\n")
with open(os.path.join(dst, "empty"), "w") as f:
    f.write("")
PY
    export FUZZ_MAP=$SEED_TMP/good_times.scn
    MAX_LEN=100000
    ;;
full)
    cp "$SEED_TMP"/*.scn "$CORPUS"/
    export FUZZ_MAP=$SEED_TMP/good_times.scn
    MAX_LEN=2500000
    ;;
mapcells)
    python3 - "$CORPUS" <<'PY'
import os
import sys

out, = sys.argv[1:2]
for i in range(16):
    data = bytes(((i * 37 + j * 11) & 0xff) for j in range(32))
    with open(os.path.join(out, "seed-%02d" % i), "wb") as f:
        f.write(data)
PY
    export FUZZ_MAP=$SEED_TMP/good_times.scn
    MAX_LEN=64
    ;;
*)
    echo "FUZZ_MODE must be fast, full, or mapcells" >&2
    exit 2
    ;;
esac

ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:halt_on_error=1}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
export ASAN_OPTIONS UBSAN_OPTIONS
"$BIN" -max_total_time="$SECONDS" -max_len="$MAX_LEN" -timeout=15 \
    -rss_limit_mb=2048 -artifact_prefix=/tmp/fuzz-artifacts/ \
    -dict="$DICT" -print_final_stats=1 "$CORPUS"
