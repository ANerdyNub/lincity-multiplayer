# Test harness

Minimal, self-contained tests for the audit fixes in `AUDIT_TODO.md`.
Nothing here is part of the installed game.

## Unit tests

```sh
tests/run_unit.sh
# or, after configure:
make check
```

The runner compiles the header/helper tests and the focused translation-unit
tests (network queues, save parsing, market/substation bookkeeping, power
grids, the P21 `do_market()` corner/edge test, P24 `opendir()` failure, and
P25 `load_graphic()` validation). `make check` invokes this script through
`check-local`. The P21, P24, and P25 tests build production translation units
with AddressSanitizer and UndefinedBehaviorSanitizer; a compiler without those
runtimes is not supported by those tests. With `xlincity` built, the runner
also checks that batch startup survives `HOME` being unset (P23). P23 now
uses the passwd home (`getpwuid(getuid())->pw_dir`), not the install-time
`LIBDIR`, and prints both that home and the exact save path written.

## Network repro

```sh
# Xephyr/X11 must be available.  The script starts its own game host.
DISPLAY=:99 tests/run_network_repro.sh \
    --payload '66 100000 0 0' --expect alive

# Multiple payloads are sent in order, with a short delay.
DISPLAY=:99 tests/run_network_repro.sh \
    --payload '66 10 10 71' --payload '70 10 10 256' \
    --expect alive --require-log accept
```

The script:

1. starts `xlincity --host N` with a temporary `HOME` and `LINCITY_HOME`
   pointing at this tree; `LINCITY_NETWORK_HOST=N` is accepted as an
   equivalent environment variable; the client sends `HELLO 2` and the
   host verifies it before sending any welcome or map bytes;
2. waits for `network: host_start OK` and never pokes `/proc/<pid>/mem` or
   sends lobby keystrokes; the old `xdotool` dependency is gone;
3. starts the required number of clients (`N=2` for `--never-read`, otherwise
   `N=1`); the host sends the map once the fixed player count is connected;
4. checks whether the host is still alive, and with `--require-log reject` or
   `--require-log accept` whether the host logged a rejection;
5. optionally requires an exact log line with `--require-line TEXT`;
6. after the payloads, reads broadcasts and asserts them with
   `--expect-build X,Y,TYPE`, `--expect-bulldoze X,Y`, and
   `--expect-mflags X,Y,FLAGS`.

The fixed count is the auto-start threshold.  There is no lobby `s` keystroke,
so the host cannot accidentally receive an in-game save-dialog key after
connecting a client.  `--origin N` is now a host CLI option; the near-edge
matrix no longer writes `main_screen_originx/y` through `/proc`.  `--seed N`
makes `--host` start a deterministic map; the matrix uses seed 2 for every
host case.  If the host dies before listening, the runner retries once with a
logged warning before failing; the headless path still creates an X window,
so it does require an X display.

`--expect crash` is used to record the pre-fix C1/C2 baseline.

By default it uses `./xlincity` and `DISPLAY=:99`. Override with
`--binary`, `--display`, or the `LINCITY_BIN` / `DISPLAY` environment
variables. The host and client logs are printed on failure.

### Full matrix

`tests/run_network_matrix.sh` runs all 32 host/client cases with per-case
logs and ASan/UBSan report directories. An X display is required.

```sh
xvfb-run -a tests/run_network_matrix.sh --binary ./xlincity \
    --log-dir /tmp/lincity-network-matrix
```

On a desktop with an existing display, omit `xvfb-run`. The CI workflow
`.github/workflows/ci.yml` runs `make check` in one job and this matrix on
an ASan+UBSan build under `xvfb-run` in another.

### Edge matrix

The ASan+UBSan run uses these payloads against one host per case:

Every row runs with `--seed 2`.  Accept rows additionally require the
broadcast shown in the right-hand column; a host that survives but fails to
apply the action is a failure.

| Case | Payloads | Required effect |
|---|---|---|
| C1 | `66 100000 0 0` | reject `MSG_BUILD position` |
| C2 bulldoze | `68 100000 0 0` | reject `MSG_BULLDOZE position` |
| C2 MFLAGS | `70 100000 0 0` | reject `MSG_MFLAGS position` |
| negative coords | `66 -1 0 0`, `68 -1 0 0`, `70 -1 0 0` | reject `MSG_BUILD position` |
| size 1 past edge | `66 100 0 80` | reject `MSG_BUILD position` |
| size 2 past edge | `66 99 0 71` | reject `MSG_BUILD position` |
| size 3 past edge | `66 98 0 61` | reject `MSG_BUILD position` |
| size 4 past edge | `66 97 0 332` | reject `MSG_BUILD position` |
| corner | `66 99 99 71`, `68 99 99 0`, `70 99 99 0` | reject `MSG_BUILD position` |
| outer-ring transport (accept) | `68 0 0 0`, `66 0 0 80`, `68 0 1 0`, `66 0 1 130`, `68 0 2 0`, `66 0 2 110`, `68 0 3 0`, `66 0 3 1`, `68 99 0 0`, `66 99 0 80`, `68 99 1 0`, `66 99 1 130`, `68 99 2 0`, `66 99 2 110`, `68 99 3 0`, `66 99 3 1`, `68 4 0 0`, `66 4 0 80`, `68 5 0 0`, `66 5 0 130`, `68 6 0 0`, `66 6 0 110`, `68 7 0 0`, `66 7 0 1`, `68 4 99 0`, `66 4 99 80`, `68 5 99 0`, `66 5 99 130`, `68 6 99 0`, `66 6 99 110`, `68 7 99 0`, `66 7 99 1` | 16 `MSG_BUILD` broadcasts at the listed x/y/type |
| near-edge transport (accept; `--origin 74`) | `68 98 10 0`, `66 98 10 80`, `68 10 98 0`, `66 10 98 130`, `68 98 98 0`, `66 98 98 110`, `68 97 97 0`, `66 97 97 1` | 4 `MSG_BUILD` broadcasts |
| type = NUM_OF_TYPES | `66 10 10 400` | reject `MSG_BUILD type` |
| negative type | `66 10 10 -1` | reject `MSG_BUILD type` |
| huge coordinate | `66 999999999999999999999 0 0` | reject malformed action line |
| huge flag value | `70 0 0 999999999999999999999` | reject malformed action line |
| MFLAGS wrong tile (reject) | `70 0 0 256` | reject `MSG_MFLAGS group` |
| MFLAGS full market flags (accept) | `66 2 2 71`, `70 2 2 1048320` | build and MFLAGS `2,2,1048320` (all 12 trade bits) |
| MFLAGS 7 trade + coverage (accept) | `66 2 2 71`, `70 2 2 7372544` | build and MFLAGS `2,2,32512` (only `0x7f00` trade bits merged) |
| MFLAGS single trade bit (accept) | `66 2 2 71`, `70 2 2 256` | build and MFLAGS `2,2,256` |
| MFLAGS live port (accept) | `66 37 76 161`, `70 37 76 7372544` | port build and MFLAGS `37,76,32512` |
| build on occupied tile (reject) | `66 2 2 71`, `66 2 2 71` | first build broadcast only; reject `MSG_BUILD placement` |
| build on market sub-tile (reject) | `66 2 2 71`, `66 3 3 71` | first build broadcast only; reject `MSG_BUILD placement` |
| valid build + bulldoze (accept) | `66 2 2 71`, `68 2 2 0` | build and bulldoze broadcasts |

### MSG_MFLAGS payload capture

The market/port callback (`do_market_cb_template()` in `src/mouse.c`)
sends `MP_INFO(mcbx,mcby).flags` as the whole word. A live market at
`(89,53)` in a hosted game had `flags=0xfff00` (all twelve trade bits).
A market or port covered by a fire station, health centre, or cricket
pitch also has `FLAG_FIRE_COVER`/`FLAG_HEALTH_COVER`/`FLAG_CRICKET_COVER`,
so a realistic payload is `0x707f00` (7372544 decimal): seven trade bits
plus all three coverage bits. `0x7fff00` (8388352) would be all twelve
trade bits plus coverage; that is the covered full-market variant. The
full-market matrix row sends `0xfff00` (1048320) explicitly, so all twelve
trade bits are covered. `mcbx/mcby` were the building origin `(89,53)`, not
a `CST_USED` sub-tile: the callback is reached only from
`MP_GROUP(mod_x,mod_y)` after `mod_x/mod_y` are resolved through
`MP_INFO(x,y).int_1/int_2`.

Because the payload includes non-trade bits, `MSG_MFLAGS` must preserve
the existing tile flags and merge only `FLAG_TRADE_MASK`; rejecting any
non-trade bit would reject genuine GUI toggles. `merge_trade_flags()` in
`src/validation.h` is tested by `tests/test_validation.c`: a tile carrying
fire/health/cricket coverage, river, power, and multitransport bits keeps
every one of them while only its `0x7f00` trade bits change.

### Placement validation

`placement_ok()` in `src/engine.c` requires the whole `size × size`
footprint to be inside the map and every cell to be `CST_GREEN`; the mouse
and `place_item()` share it. The matrix builds on an occupied market tile
and on a `CST_USED` sub-tile, both of which are rejected, and keeps a valid
build/bulldoze control.

A normal click is clamped to `1..98`, but the drag path can reach 0/99:
`pixel_to_mappoint()` writes the coordinate before returning false for a
point outside the main window, and `mt_draw()` ignores that return.  The
outer-ring cases therefore exercise the restored `connect_transport()`
`x±1`/`y±1` guards (P22) rather than being rejected by `placement_ok()`.

The near-edge row is run with `--origin 74`, which pokes
`main_screen_originx/y` in the running host.  This deterministically covers
the M12 redraw edge and also exercises P22 at 98.  M12 is fixed by
`c549b43`; keep the row as a regression case rather than deleting it.

Every host row uses `--seed 2`; the map is therefore identical from run to
run.  `(2,2)` is green outside the starting village.  The seeded map has a
verified live port bank at `(37,76)`: `is_real_river(41,76..79)` succeeds, so
the live port MFLAGS case exercises the real market/port group path.  Earlier
matrix versions relied on `(2,2)` being outside a *random* river/village and
only checked host survival; that let M15 hide behind failed outer-ring builds.
Reject rows now pass `--require-line` with the exact reason
(`MSG_BUILD position`, `MSG_BUILD type`, `MSG_BUILD placement`,
`MSG_BULLDOZE position`, `MSG_MFLAGS position`, `MSG_MFLAGS group`, or
`malformed action line`), not the generic `network: rejected` prefix.

## Protocol version and map encoding (P12)

```sh
# Same protocol: the map transfers and a normal build is accepted.
DISPLAY=:99 tests/run_network_repro.sh --seed 2 \
    --payload '66 2 2 71' --expect-build 2,2,71

# Old host-side hello is rejected before any map bytes.
DISPLAY=:99 tests/run_network_repro.sh --hello-version 1 \
    --expect-hello-rejected

# New host-side hello is rejected the same way.
DISPLAY=:99 tests/run_network_repro.sh --hello-version 3 \
    --expect-hello-rejected

# A client that connects but never sends HELLO is dropped without a slot.
DISPLAY=:99 tests/run_network_repro.sh --no-hello

# A host advertising an old welcome version is rejected by the real client.
DISPLAY=:99 tests/run_client_repro.sh --version-mismatch
```

The wire format is now protocol version **2**. The client sends
`72 2` (`MSG_HELLO <version>`) immediately after connecting. The host reads
that full line before it accepts the client into the lobby or sends a map;
malformed, old, and new versions get `88` (`MSG_DISCONN`) and a close. The
host welcome is `87 100 2` (`MSG_WELCOME <world_side_len> <version>`), and
the client rejects any other version before reading the compressed map.

The compressed map payload is 480000 bytes of fixed-width little-endian
fields: `int16` type and group per cell, a 40-byte info record (`int32
population`, `int32 flags`, `uint16 coal_reserve`, `uint16 ore_reserve`,
seven `int32` values), and `int32` pollution per cell. The shared
`map_state_ok()` validator checks every type/group pair, every `CST_USED`
origin/footprint reference, and optional saved market/substation lists before
the client or save loader uses the map.

## Client action validation

```sh
DISPLAY=:99 ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
tests/run_client_repro.sh \
    --binary /tmp/opencode/lincity-asan-current/xlincity \
    --payload '66 100000 0 0' \
    --payload '68 100000 0 0' \
    --payload '70 100000 0 0' \
    --expect alive --require-log reject --expect-rejects 3
```

`tests/fake_host.py` accepts one client, requires `HELLO 2` (`72 2`),
sends the numeric welcome `87 100 2`, a valid 480000-byte zeroed map, then
the supplied action lines.
The real client joins `127.0.0.1:5123` through the same `network_flag` poke
used by the host harness (key `2`, then the default host and port).  The
fake host does not read client actions.  This is the P6 repro: all three
oversized actions must be ignored and the client must stay alive.  The port
must be free; stop any other test host first.  `--require-log reject`
requires exactly `--expect-rejects` client `network: rejected` lines, so a
silent client cannot pass just by staying alive.  The 16-payload edge matrix
expects 15 rejects; `(99,99)` bulldoze is a valid boundary action and has no
reject line.

### Network read limits (P7)

```sh
DISPLAY=:99 tests/run_client_repro.sh \
    --binary /tmp/opencode/lincity-asan-current/xlincity --bad-map

DISPLAY=:99 tests/run_client_repro.sh \
    --binary /tmp/opencode/lincity-asan-current/xlincity \
    --partial --payload '66 1 1 71'
```

`--bad-map` sends `usz=480000, csz=0xffffffff` with no map payload.  The
client must reject it with `compressed world length out of range` before
allocating the blob, then remain alive.  `--partial` sends the final action
line without a newline and keeps the socket open; the client's 5000 ms
`NETWORK_IO_TIMEOUT_MS` deadline must log `line read timed out` and the
client must remain alive.  The normal command above is the valid-map
control and must still print `network: recv_map: done OK`.

### Host input and slow clients

```sh
DISPLAY=:99 tests/run_network_repro.sh \
    --binary /tmp/opencode/lincity-asan-current/xlincity --slowline
```

The host keeps a per-client 4 KiB line buffer fed by non-blocking reads, so a
partial line cannot stop the simulation.  `--slowline` sends one byte every
four seconds for 30 s while the test client drains broadcasts; the client
requires at least 50 host messages and reports the actual count.  The current
headless run received 10,897 messages in 30.0 s (~363 msg/s), up from 343
messages (~11.3 msg/s) when the harness's post-connect `s` opened a modal
dialog.

`--never-read` is the send-side repro.  It starts **two** clients: a client
that reads the map and then stops reading, plus a normal read-only client.
The host must keep the normal client served while the first is stalled.  The
normal client must receive at least 50 messages; the host must log:

```
network: rejected malformed action line
```

The probe is `999\n`, which parses as one field (action > 255).  It therefore
reaches `network_bad_action()`, not the client-side validator.  That host log
is intentionally rate-limited to one line per second, so the test expects one
probe, not one line per byte.

The two-client run also exposed a test-harness bug: with the lobby max at 2
the host auto-starts, so the later `s` keystroke was an in-game action and
blocked the loop.  The harness now skips `s` for `--never-read`.

`--overlong` sends 5000 bytes without a newline.  The host logs
`network: host line too long; disconnecting client fd=...` and closes only
that client.  The client accepts either EOF or a reset as the disconnect.

Overflow policies:

- Host receive line > 4 KiB without a newline: disconnect that client.
- Host transmit queue > 1 MiB: mark the client dead and disconnect it; the
  socketpair test `tests/test_host_queue.c` forces this with a 1 KiB
  test-only bound.
- Client transmit queue > 8 KiB: increment the drop counter, set the resync
  flag, and log `network: client send queue full; action lost; reconnect to
  resync`.  The main loop surfaces the same message.  This deliberately asks
  the user to reconnect; there is no automatic map-stream resync yet.

### P10 build cost authority

```sh
DISPLAY=:99 tests/run_network_repro.sh \
    --binary /tmp/opencode/lincity-asan-matrix/xlincity --cost
```

`tests/cost_client.py` starts at balance 0 and sends a parkland build
(type 91, cost 1000, no-credit group) followed by a market build (type 71,
cost 100).  The host must reject the parkland with
`network: rejected MSG_BUILD placement (4,4,-1)`, must not broadcast it, and
the client must see the initial balance 0 and then exactly -100 after the
market.  The client then bulldozes that market and requires exactly -200:
`bulldoze_item()` charges `main_groups[target_group].bul_cost`
(`GROUP_MARKET_BUL_COST` = 100), independent of any UI selection, and the
host now sends `MSG_MONEY` immediately after a successful bulldoze too.
`place_item()` charges `get_type_cost(type)`, not the host's
`selected_module_cost`.

## Action gating (P11)

`tests/test_action_gate.c` calls the centralized helpers used by the remote
handler, ordinary mouse placement, drag/overwrite drag, and area bulldoze:
`network_host_broadcast_build_if_placed()` only enqueues `MSG_BUILD` for a
zero `place_item()` result, and
`network_host_broadcast_bulldoze_if_removed()` only enqueues `MSG_BULLDOZE`
for a positive `bulldoze_item()` result.  Failed results must leave the
socket empty.  This is the regression gate for H10; the drag path no longer
broadcasts a build or overwrite that the engine rejected.

Groups already flagged no-credit (`GROUP_POWER_SOURCE`, `GROUP_UNIVERSITY`,
`GROUP_PARKLAND`, `GROUP_RECYCLE`, `GROUP_ROCKET`, or `.no_credit`) require
the current balance to cover the full requested cost.  Other groups keep the
existing credit-building behavior: requiring every group to cover its cost
would reject every build in a new city, whose balance starts at 0.

### P8 client send queue

`tests/run_unit.sh` builds `tests/test_send_queue.c` with the network
translation unit and fills a socketpair until the 8 KiB client queue drops an
action.  It then drains the peer, retries, and requires delivery to resume.
The old `EAGAIN`-as-success path is gone; drops are counted, the resync flag
is checked, and delivery resumes after the queue drains.

`tests/test_host_fds.c` covers P9: dropping the primary or an extra client
clears exactly one host slot, decrements `network_num_clients` once, and does
not make it negative when the same fd appears twice in a snapshot.


## Save loader checks

```sh
# Truncated save: clean error, current city survives.
DISPLAY=:99 tests/run_save_fuzz.sh --binary ./xlincity --expect alive \
    --require-log 'truncated or invalid save file'

# Shipped version-98 opening saves still load.
DISPLAY=:99 tests/run_save_fuzz.sh --binary ./xlincity \
    --fixture opening/good_times.scn --save-name 1_good --expect alive \
    --require-log 'load_city: loaded' \
    --forbid-log 'truncated or invalid save file'
DISPLAY=:99 tests/run_save_fuzz.sh --binary ./xlincity \
    --fixture opening/bad_times.scn --save-name 1_bad --expect alive \
    --require-log 'load_city: loaded' \
    --forbid-log 'truncated or invalid save file'

# Mutate one line of a valid save to exercise P14 count/coordinate checks.
DISPLAY=:99 tests/run_save_fuzz.sh --binary ./xlincity \
    --fixture opening/good_times.scn --save-name 1_badmc \
    --set-line 422054 513 --expect alive \
    --require-log 'truncated or invalid save file'
```

The default fixture is the audit's 24-byte gzip of `112\n`, placed in save
slot 2. `--fixture` copies a real save; `--set-line N VALUE` rewrites one
1-based line after gunzip; `--save-name` chooses the slot (its first character
must be 1-9). `--expect crash` remains available for the pre-fix H3 baseline.
The script uses `xdotool` and an X display, and waits/retries the load dialog
so a slow window does not create a false pass.

## Baseline

`tests/BASELINE.md` records what the harness is expected to reproduce at
`89c29a7` before the fixes.

## Headless save round trips

`tests/run_save_roundtrip.sh` uses the non-interactive batch hooks:

```sh
./xlincity --load input.gz --save output.gz --ticks 0
```

It gzips each file in `tests/fuzz_save_corpus/`, loads it, saves it through the
real writer, and compares the result with the original using
`tests/save_roundtrip.py`. Only documented derived fields are masked: the
obsolete `diff_old_population`, old monthgraph size/padding, removed monthgraph
fields, screen origin, animation/power-grid identifiers, and
`update_tech_dep()` outputs. The runner also requires `bad_times` to preserve
`tpopulation=235358` and `tunemployed_population=788`. `tests/run_unit.sh`
runs this after the unit tests; no X display or `xdotool` is needed.

```sh
tests/run_save_roundtrip.sh --binary ./xlincity
```

## Transactional save rollback

`tests/run_save_rollback.sh` loads a valid opening save with `--load`, saves a
control D, then uses the test-only `--bad-load` hook for five corrupt variants:
early map truncation, late map truncation, bad market count, bad substation
count, and a corrupt final-section scalar. After each rejection it saves C and
requires D and C to match after gunzip. `tests/run_unit.sh` runs this guard; no
X display or `xdotool` is needed. `--bad-load` is not a privilege boundary: it
runs the same `load_city()` and rollback path as a normal failed load, only
after a successful `--load`; it merely exposes the result to the test.

```sh
tests/run_save_rollback.sh --binary ./xlincity \
    --fixture opening/good_times.scn
```

## Power-grid behavior

`tests/run_power_behavior.sh` runs a multi-grid save for 300 ticks with the
current build and a baseline build containing the pre-P18 `src/power.c`. It
compares the count of map tiles with `FLAG_POWERED`, the grid ID of every
power cell keyed by coordinate, grid composition, and the saved power-entry
list. The default `opening/bad_times.scn` has four grid IDs: one main grid
with 13 substations and three isolated windmill grids. The 17 entries in the
saved `numof_substations` list are 13 substations plus 4 windmills.

The script requires an explicit baseline:

```sh
tests/run_power_behavior.sh --baseline /path/to/pre-P18-xlincity \
    --current ./xlincity
```

`power_made`/`power_used` are compared too, but only as legacy saved fields:
no simulation code updates them, so they do not describe grid output or
demand.

## Save parser fuzzing

`tests/fuzz_save_parser.c` is a libFuzzer target for the save parser and
`map_state_ok()`. Build/run it with a Clang that provides libFuzzer:

```sh
CC=/path/to/clang tests/run_fuzz_save.sh 900
FUZZ_MODE=full CC=/path/to/clang tests/run_fuzz_save.sh 900
FUZZ_MODE=mapcells CC=/path/to/clang tests/run_fuzz_save.sh 900
```

Fast mode caches a valid 100x100 map once and fuzzes the real save suffix
(origin, coordinate tables, counts, and scalar/tail section); it reaches
thousands of executions per second. Full mode feeds the complete text save
through `save_read_map()` and is deliberately slower.

Mapcells starts from the cached valid map, mutates up to eight cells
(type/group/`Map_Point_Info`, forcing the `CST_USED` origin and footprint
paths first), and runs the full `map_state_ok()` validator after each
mutation. It uses the real `lintypes.c` type-to-group mapping and group
sizes, so the opening map itself validates. The final 900-second run used
184,537 inputs at 204/s, reached 79 edges, and produced no sanitizer or
fuzzer artifact.

A merged fast+mapcells `llvm-cov` report (generated locally, not committed)
showed 97.50% of `save_parse.h` lines and 68.75% of `validation.h` lines
covered. After combining the unit tests, `tests/test_save_parse.c` covers the
two former `save_parse.h` gaps and `tests/test_validation.c` covers all
remaining `validation.h` paths, including
`map_used_cell_ok()`/`map_state_ok()` failure returns and the saved
market/substation lists. The combined report showed both headers 100% covered.

The mapcells path deliberately passes no lists (`-1, NULL, NULL`) while
mutating map cells, so it cannot exercise lines 163-191; the full suffix path
reads the corpus's valid lists, and its random scalar mutations do not target
list coordinates. Mutated map cells usually fail the earlier type/group or
origin checks first. The list branches are therefore covered by the targeted
unit tests, not by mapcells; the 15-minute run was not a seed-corpus-only
measurement.

The wrapper derives its fast corpus from the compressed full saves in
`tests/fuzz_save_corpus/` (opening scenes plus writer-produced saves) and uses
`save.dict`. It runs with ASan+UBSan and `halt_on_error=1` by default. The GUI
loader is covered separately by `run_save_fuzz.sh` and
`run_save_rollback.sh`.
