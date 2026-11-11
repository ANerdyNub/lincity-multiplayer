# Pre-fix baseline (`89c29a7`)

These are the results the P0 harness is meant to reproduce before the
corresponding patches. They are recorded from the audit repro bundle in
`/tmp/opencode/audit-repros/` so a clean checkout has the expected outcomes
next to the scripts.

| Case | Input | Baseline result | Evidence |
|---|---|---|---|
| C1 | `66 100000 0 0` (`MSG_BUILD`, x=100000) | host exits with `CRASH: signal 11` | `logs/c1_host_crash.log` |
| C2 | `68 100000 0 0` (`MSG_BULLDOZE`, x=100000) | host exits with `CRASH: signal 11` | `logs/c2_host_crash.log` |
| C2 | `70 100000 0 0` (`MSG_MFLAGS`, x=100000) | same unchecked `MP_INFO(ax,ay).flags` write | `logs/c2_host_crash.log` |
| H3 | 24-byte gzip of `112\n` in save slot 2 | loader crashes after EOF | `logs/h3_truncated_save_crash.log` |
| M8 | `pclose(NULL)` from `fclose_read_gzipped(NULL)` | SIGSEGV, exit 139 | `scripts/m8_pclose_null.c` |
| M1 | `env -u HOME ./xlincity` | NULL dereference in `init_path_strings()` | session log, 2026-11-07 |

Expected after the patches: C1/C2 stay alive and log a rejection; H3 loads
with a clean error; M8 never reaches `pclose(NULL)`; M1 falls back to
`LIBDIR`.
