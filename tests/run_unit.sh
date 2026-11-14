#!/bin/sh
# Compile and run the focused unit tests.
# P0: test_validation.c does not exist yet; report a skip instead of failing.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}

if [ ! -f "$ROOT/tests/test_validation.c" ]; then
    echo "tests/run_unit.sh: tests/test_validation.c not present; skipping (P0)"
    exit 0
fi

"$CC" -I "$ROOT/src" -Wall -Wextra -Werror \
    -o "$ROOT/tests/test_validation" "$ROOT/tests/test_validation.c"
"$ROOT/tests/test_validation"

# L4: num_to_ansi() must treat its size argument as the buffer size.
if [ -f "$ROOT/tests/test_lclib_size4.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror -Wno-format-truncation -Wno-format-overflow \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_lclib_size4" \
        "$ROOT/tests/test_lclib_size4.c" "$ROOT/src/lclib.c" -lm
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_lclib_size4"
fi

# P12: the production fixed-width codec must match a hard-coded golden
# buffer and round-trip the same type/group/info/pollution arrays.
# P15: invalid map type/group entries must not index outside the count array.
if [ -f "$ROOT/tests/test_group_count.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_group_count" \
        "$ROOT/tests/test_group_count.c" "$ROOT/src/simulate.c" -lz -lm
    "$ROOT/tests/test_group_count"
fi

# P14 fuzz finding: the unlocked scanner must handle INT_MIN without signed
# overflow and reject values outside int range.
if [ -f "$ROOT/tests/test_save_int.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -o "$ROOT/tests/test_save_int" "$ROOT/tests/test_save_int.c"
    "$ROOT/tests/test_save_int"
fi

if [ -f "$ROOT/tests/test_save_parse.c" ]; then
    "$CC" -I "$ROOT/src" -Wall -Wextra -Werror \
        -o "$ROOT/tests/test_save_parse" "$ROOT/tests/test_save_parse.c"
    "$ROOT/tests/test_save_parse"
fi

# Data-mod loader: INI parser, discovery, duplicate IDs, rollback, and
# synthetic map validation through the shared registry.
if [ -f "$ROOT/tests/test_mod_loader.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_mod_loader" \
        "$ROOT/tests/test_mod_loader.c" \
        "$ROOT/src/mod_ini.c" "$ROOT/src/mod_loader.c" \
        "$ROOT/src/mod_registry.c"
    "$ROOT/tests/test_mod_loader"
fi

# M3: page 0 stays frozen; mod entries group by category, order stably,
# paginate at 32, and keep mod help state out of the legacy slot arrays.
if [ -f "$ROOT/tests/test_mod_menu.c" ]; then
    "$CC" -I "$ROOT/src" -Wall -Wextra -Werror \
        -o "$ROOT/tests/test_mod_menu" \
        "$ROOT/tests/test_mod_menu.c" "$ROOT/src/mod_menu.c"
    "$ROOT/tests/test_mod_menu"
fi

if [ -f "$ROOT/tests/test_map_wire.c" ]; then
    "$CC" -I "$ROOT/src" -Wall -Wextra -Werror \
        -o "$ROOT/tests/test_map_wire" "$ROOT/tests/test_map_wire.c"
    "$ROOT/tests/test_map_wire"
fi

# L5: a valid map must be followed by MSG_OK.  Compile only the network
# translation unit; --gc-sections drops the unused host/UI functions.
if [ -f "$ROOT/tests/test_recv_map_ok.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_recv_map_ok" \
        "$ROOT/tests/test_recv_map_ok.c" "$ROOT/src/network.c" -lz
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_recv_map_ok"
fi

# L1: MPS writers must bounds-check their slot and clamp truncated lengths.
if [ -f "$ROOT/tests/test_mps_bounds.c" ]; then
    "$CC" -DHAVE_CONFIG_H -DLC_X11=1 -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-implicit-fallthrough -Wno-format-truncation \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_mps_bounds" \
        "$ROOT/tests/test_mps_bounds.c" "$ROOT/src/mps.c" -lz -lm
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_mps_bounds"
fi

# M14: row 0 and column 0 must still receive connection graphics.
if [ -f "$ROOT/tests/test_connect_transport_row0.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror -Wno-implicit-fallthrough \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_connect_transport_row0" \
        "$ROOT/tests/test_connect_transport_row0.c" \
        "$ROOT/src/shrtypes.c" -lz
    "$ROOT/tests/test_connect_transport_row0"
fi

# P28: client requests must not mutate the map; only a host broadcast may.
if [ -f "$ROOT/tests/test_client_requests.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_client_requests" \
        "$ROOT/tests/test_client_requests.c" "$ROOT/src/network.c" -lz
    "$ROOT/tests/test_client_requests"
fi

# P8: force the client send queue full over a socketpair.  Compile only the
# network translation unit; --gc-sections drops the unused host/UI functions.
if [ -f "$ROOT/tests/test_send_queue.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections \
        -Wl,--gc-sections \
        -o "$ROOT/tests/test_send_queue" \
        "$ROOT/tests/test_send_queue.c" "$ROOT/src/network.c" -lz
    "$ROOT/tests/test_send_queue"
fi

# P8b: force the host transmit queue full with a small test-only bound.
if [ -f "$ROOT/tests/test_host_queue.c" ]; then
    "$CC" -DHAVE_CONFIG_H -DNETWORK_HOST_TX_BYTES=1024 \
        -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections \
        -Wl,--gc-sections \
        -o "$ROOT/tests/test_host_queue" \
        "$ROOT/tests/test_host_queue.c" "$ROOT/src/network.c" -lz
    "$ROOT/tests/test_host_queue"
fi

# P11: failed placement/bulldoze results must not be broadcast.
if [ -f "$ROOT/tests/test_action_gate.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections \
        -Wl,--gc-sections \
        -o "$ROOT/tests/test_action_gate" \
        "$ROOT/tests/test_action_gate.c" "$ROOT/src/network.c" -lz
    "$ROOT/tests/test_action_gate"
fi

# P9: client-slot cleanup and network_num_clients bookkeeping.
if [ -f "$ROOT/tests/test_host_fds.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections \
        -Wl,--gc-sections \
        -o "$ROOT/tests/test_host_fds" \
        "$ROOT/tests/test_host_fds.c" "$ROOT/src/network.c" -lz
    "$ROOT/tests/test_host_fds"
fi

# P17: shuffling with zero or one market/substation must not divide by zero.
if [ -f "$ROOT/tests/test_shuffle.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" -I "$ROOT/src/modules" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_shuffle" \
        "$ROOT/tests/test_shuffle.c" "$ROOT/src/market.c" \
        "$ROOT/src/modules/substation.c" -lz -lm
    "$ROOT/tests/test_shuffle"
fi

# P18: corrupt substation counts and int_6 values must not index past
# fixed-size arrays.
if [ -f "$ROOT/tests/test_power.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_power" \
        "$ROOT/tests/test_power.c" "$ROOT/src/power.c" -lz -lm
    "$ROOT/tests/test_power"
fi

# P16: removing the last market/substation must not read one past the array.
if [ -f "$ROOT/tests/test_remove.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" -I "$ROOT/src/modules" \
        -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections \
        -Wl,--gc-sections \
        -o "$ROOT/tests/test_remove" \
        "$ROOT/tests/test_remove.c" "$ROOT/src/market.c" \
        "$ROOT/src/modules/substation.c" -lz -lm
    "$ROOT/tests/test_remove"
fi

# P21/M3: do_market() must not probe outside the map at a corner or edge.
if [ -f "$ROOT/tests/test_market_edges.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_market_edges" \
        "$ROOT/tests/test_market_edges.c" "$ROOT/src/market.c" -lz -lm
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_market_edges"
fi

# P23/M1: with HOME unset, startup must use getpwuid(), not LIBDIR.
if [ -x "$ROOT/xlincity" ]; then
    P23_TMP=$(mktemp -d)
    P23_HOME=$(awk -F: -v u="$(id -u)" '$3 == u { print $6; exit }' /etc/passwd)
    test -n "$P23_HOME"
    ln -s "$ROOT/colour.pal" "$P23_TMP/colour.pal"
    ln -s "$ROOT/icons" "$P23_TMP/icons"
    ln -s "$ROOT/opening" "$P23_TMP/opening"
    env -u HOME LINCITY_HOME="$P23_TMP" "$ROOT/xlincity" \
        --load "$ROOT/opening/good_times.scn" \
        --save "$P23_TMP/out.scn" --ticks 1
    test -s "$P23_TMP/out.scn"
    echo "P23: HOME unset; getpwuid home=$P23_HOME; wrote $P23_TMP/out.scn"
    if [ -d "$P23_HOME/.lincity" ]; then
        echo "P23: default save directory $P23_HOME/.lincity"
    fi
    rm -rf "$P23_TMP"
fi

# P24/M6: a failed opendir() must not reach readdir(NULL).
if [ -f "$ROOT/tests/test_save_dir_opendir.c" ]; then
    "$CC" -DHAVE_CONFIG_H -DLC_X11=1 -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-format-truncation \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_save_dir_opendir" \
        "$ROOT/tests/test_save_dir_opendir.c" "$ROOT/src/ldsvgui.c" -lz -lm
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_save_dir_opendir"
fi

# P25/M7: missing, empty, and short graphics must fail cleanly.
if [ -f "$ROOT/tests/test_load_graphic.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_load_graphic" \
        "$ROOT/tests/test_load_graphic.c" "$ROOT/src/fileutil.c" -lz
    ASAN_OPTIONS=detect_leaks=0 "$ROOT/tests/test_load_graphic"
fi

# L7/M8: a missing save file must return NULL, and close(NULL) is a no-op.
if [ -f "$ROOT/tests/test_pclose_null.c" ]; then
    "$CC" -DHAVE_CONFIG_H -I "$ROOT" -I "$ROOT/src" \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o "$ROOT/tests/test_pclose_null" \
        "$ROOT/tests/test_pclose_null.c" "$ROOT/src/fileutil.c" -lz
    "$ROOT/tests/test_pclose_null"
fi

# Headless save tests: corpus round trips and transactional failed reloads.
if [ -x "${LINCITY_ASAN_BIN:-$ROOT/xlincity}" ] \
    && command -v python3 >/dev/null 2>&1; then
    "$ROOT/tests/run_mod_loader.sh" \
        --binary "${LINCITY_ASAN_BIN:-$ROOT/xlincity}"
    "$ROOT/tests/run_save_roundtrip.sh" \
        --binary "${LINCITY_ASAN_BIN:-$ROOT/xlincity}"
    "$ROOT/tests/run_save_rollback.sh" \
        --binary "${LINCITY_ASAN_BIN:-$ROOT/xlincity}" \
        --fixture "$ROOT/opening/good_times.scn"
    "$ROOT/tests/run_save_paths.sh" \
        --binary "${LINCITY_ASAN_BIN:-$ROOT/xlincity}"
else
    echo "SKIP: headless save tests (needs xlincity and python3)"
fi
