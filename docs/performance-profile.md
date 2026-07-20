# edge-healthd Performance Profile

## Profiles

| Version | Board | Kernel | Date | Burst | Syscalls/cycle |
|---------|-------|--------|------|-------|----------------|
| 0.1.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.6.63-v8-16k-igw | 2026-02-xx | 227 ms | 5,133 |
| 0.4.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.18.13-v8-16k-igw | 2026-03-03 | 98 ms | 683 |
| 0.5.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.18.13-v8-16k-igw-00005-g519f4a2b7bf8 | 2026-04-08 | 49 ms | 904 |
| 0.5.1   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.18.13-v8-16k-igw-00005-gba42df28bfdb | 2026-04-09 | **12 ms** | **169** |
| 0.7.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.18.37-v8-16k-igw-00007-g9a0306b38b12 | 2026-07-20 | ~60 ms † | **~200** |

† Burst wall-clock is now dominated by blocking D-Bus round trips (`ppoll` waits), not CPU —
kernel time is ~1.85 ms/cycle. It is load- and D-Bus-latency-dependent and not directly
comparable to 0.5.1's fresh-device figure. The meaningful, comparable metric is syscalls/cycle.

---

## Profile: edge-healthd 0.7.0

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM), kernel
6.18.37-v8-16k-igw-00007-g9a0306b38b12. Binary: aarch64 Yocto build,
systemd-managed. Raw results: `benchmark/20260720T122040Z/` (gitignored).

This build adds the schema v1.1 observability work (per-cycle ethtool link
speed/duplex query, availability/freshness/per-domain severity) **and** replaces
the per-cycle `sd_journal_open()` model with a persistent journal reader.

### Test Configuration

```json
{
  "collect_interval_sec": 60,
  "time_sync_interval_sec": 300,
  "update_check_interval_sec": 1800,
  "monitored_services": ["sshd.socket", "systemd-networkd.service",
                          "mosquitto.service", "systemd-journald.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": []
}
```

Unlike the fresh 0.5.1 target, this device has accumulated uptime and **~15 rotated
journal files** — exactly the condition the 0.5.1 profile predicted would inflate the
old per-cycle `sd_journal_open()` cost.

### Syscall Summary (130 s window, 2 collection cycles)

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 23.50    0.000867          34        25           openat
 21.49    0.000793           6       114        36 recvmsg
 11.90    0.000439          11        38           ppoll
  8.83    0.000326           8        37           sendmsg
  5.31    0.000196           4        40         3 futex
  4.69    0.000173          10        16           newfstatat
  4.58    0.000169           5        29           close
  4.44    0.000164           6        25         2 read
  3.25    0.000120          20         6           sendto
  2.20    0.000081          20         4           socket
  1.65    0.000061           7         8           statfs
  1.38    0.000051          25         2           renameat
  1.38    0.000051           6         8           getdents64
  1.17    0.000043          21         2           readlinkat
  ...
------ ----------- ----------- --------- --------- ----------------
100.00    0.003690           9       400        41 total
```

**Totals (2 cycles):** 400 calls, 3.69 ms kernel time. **Per cycle:** ~200 calls,
~1.85 ms kernel time — back to the 0.5.1 baseline order despite more rotated journal
files and the added per-cycle ethtool query.

### Journal Scanning (0.7.0) — persistent reader

The daemon now opens the system journal **once** at startup and keeps the handle open
(`sd_journal` follows rotation while open), maintaining a bounded in-memory buffer.
`JournalProbe` and per-unit `log_excerpt` read the buffer; each cycle does one
`sd_journal_wait(0)` and reads only newly-appended entries.

| Metric (2 cycles) | 0.7.0 pre-reader (per-cycle open) | 0.7.0 persistent reader |
|---|---|---|
| journal `openat` | 170 | **2** (one-time prime) |
| `mmap` / `readlinkat` / `getdents64` | 130 / 130 / 68 | ~0 / 4 / 8 |
| `fstat` | 294 | 6 |
| total syscalls | 1,903 | **400** |

The old model did one `sd_journal_open()` per monitored unit **plus** one for the system
scan, each re-walking every rotated file — ~88 % of per-cycle `openat`. The cost scaled
with monitored-unit count and rotated-file count; it is now **independent of both**.

### Network — ethtool link query (0.7.0)

`speed_mbps`/`duplex` are populated via an ethtool generic-netlink query in the resources
cycle: ~10–12 syscalls (`socket`/`bind`/`getsockname`/`sendto ×3`/`recvmsg`/`close`), ~1.7 ms,
scaling as 1 family-resolve + N interfaces. The family id is re-resolved each cycle —
tracked for a follow-up optimisation (cache the family / reuse the socket).

### Resource Footprint

| Metric | 0.5.1 | 0.7.0 | Note |
|---|---|---|---|
| VmRSS | 5.5 MB | ~15.3 MB | persistent `sd_journal` mmaps + buffer resident |
| Open FDs (steady state) | 10 | 22 | +~10 journal fds + inotify held open (vs opened/closed per cycle) |
| Kernel time / cycle | 0.97 ms | ~1.85 ms | more probes; still negligible |
| Duty cycle (CPU) | 0.020 % | ~0.003 % | (kernel time / interval) |

The persistent handle trades ~10 held-open fds and ~10 MB RSS (well within the 64 MB cap)
for eliminating the per-cycle open churn and decoupling cost from rotated-file count.

---

## Profile: edge-healthd 0.5.1

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM) running IoT Gateway OS igw.0.1.1,
kernel 6.18.13-v8-16k-igw-00005-gba42df28bfdb.

Binary: `edge-healthd 0.5.1` tag (aarch64, Yocto build, systemd-managed).
Raw results: `benchmark/20260409T105636Z/` (gitignored).

### Test Configuration

```json
{
  "collect_interval_sec": 60,
  "time_sync_interval_sec": 300,
  "update_check_interval_sec": 1800,
  "monitored_services": ["sshd.socket", "NetworkManager.service", "mosquitto.service",
                          "telegraf.service", "influxdb.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": []
}
```

### Collection Cycle Timing

Normal cycle (no TimeSyncProbe, no UpdateProbe, DeviceProbe already ran): **~12 ms wall-clock**.
NTP cycle (every 5th minute, when `time_sync_interval_sec=300` fires): **~78 ms wall-clock**.

| Phase            | Wall-clock | Syscalls | Description                                                          |
|------------------|------------|----------|----------------------------------------------------------------------|
| Boot probe       | <1 ms      | ~5       | `boot_state.json` read                                               |
| D-Bus connect    | ~1 ms      | ~18      | socket + auth handshake to `org.freedesktop.systemd1`                |
| D-Bus services   | ~5 ms      | ~60      | 5 units × (GetUnit + 2 property calls), 5 ppoll waits                |
| Journal scans    | ~3 ms      | ~60      | 6 scans (5 services + JournalProbe); `/var/log/journal` (non-root; fresh persistent journal, minimal fstats) |
| Resources        | ~1 ms      | ~15      | `/proc/meminfo`, `sysinfo()`, `statfs × 2`, `/sys/class/thermal`    |
| Netlink + IP     | 0 ms       | 0        | in-memory cache                                                      |
| TimeSyncProbe    | **0 ms**   | **0**    | **skipped** — 300s cadence, not yet due                              |
| UpdateProbe      | **0 ms**   | **0**    | **skipped** — 1800s cadence                                          |
| DeviceProbe      | **0 ms**   | **0**    | **skipped** — once-only, already collected at startup                |
| Write snapshot   | <1 ms      | 3        | atomic tmp-write + `renameat` to `state.json`                        |
| sd_notify STATUS | <1 ms      | 1        | `sendmsg` to `/run/systemd/notify`                                   |
| **Total burst**  | **~12 ms** | **~169** | then `futex(WAIT)` until next cycle                                  |

### Syscall Summary (130 s window, 2 collection cycles)

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 26.73    0.000517          13        38        12 openat
  9.31    0.000180           2        64        22 recvmsg
  7.39    0.000143           4        34           close
  7.19    0.000139           6        22           sendmsg
  6.00    0.000116          38         3         1 futex
  5.89    0.000114           4        28           getdents64
  4.91    0.000095           4        20           ppoll
  4.08    0.000079           5        14           read
  3.67    0.000071          35         2           renameat
  3.26    0.000063           3        16           write
  3.00    0.000058           2        20           getsockopt
  2.90    0.000056          14         4           socket
  2.43    0.000047           2        16           fstat
  1.91    0.000037          37         1         1 restart_syscall
  1.86    0.000036           3        12           fstatfs
  1.65    0.000032           4         8           statfs
  1.60    0.000031          15         2           connect
  1.03    0.000020           3         6           setsockopt
  0.98    0.000019           4         4           eventfd2
  0.78    0.000015           7         2           newfstatat
  0.47    0.000009           4         2           sysinfo
  0.47    0.000009           4         2           bind
------ ----------- ----------- --------- --------- ----------------
100.00    0.001934           5       338        36 total
```

**Totals (2 cycles):** 338 calls, 1.93 ms kernel time
**Per cycle:** ~169 calls, ~0.97 ms kernel time

### Resource Footprint

| Metric                    | Value                                  |
|---------------------------|----------------------------------------|
| Threads                   | 3 (main, watchdog, D-Bus event loop)  |
| VmRSS                     | 5.5 MB                                 |
| PSS (proportional set)    | 2.5 MB                                 |
| Private dirty memory      | 436 kB                                 |
| Open FDs (steady state)   | 10                                     |
| Total CPU since boot      | 93 ms (`sum_exec_runtime`, ~12 min uptime) |
| Context switches          | 1,745 (1,711 voluntary, 34 involuntary)|
| I/O reads (cumulative)    | 1.3 MB (boot + journal scans)          |
| I/O writes (cumulative)   | 8 KB (2 snapshot writes)               |
| Duty cycle (normal)       | **0.020%** (12 ms active / 60 s interval) |
| Duty cycle (NTP cycle)    | **0.130%** (78 ms active / 60 s interval) |

### D-Bus Connection Model (0.5.1)

One ephemeral D-Bus connection is opened and closed **every cycle**:
1. `org.freedesktop.systemd1` — 5 service queries (GetUnit + unit properties per service)

The `edge.health.Manager` OverallSeverity update goes through the daemon's **long-lived
service connection** (the connection that holds the `edge.health` bus name). No new connection
is opened for property publication — this eliminates the per-cycle ephemeral second connection
that was present in v0.5.0.

Evidence: `socket`/`connect` appear 2/1 times per cycle (vs 4/2 in v0.5.0).

Remaining backlog item: *refactor(dbus): reuse systemd1 client connection per cycle* would
eliminate the last ephemeral connection.

### ProbeSchedule (0.5.1)

Probe scheduling moved into daemon (PR #26). Cadences:

| Probe          | Cadence          | Notes                                      |
|----------------|------------------|--------------------------------------------|
| DeviceProbe    | **once at start** | OS version, board ID — does not change    |
| BootProbe      | every 60 s       | consecutive failures counter               |
| ServicesProbe  | every 60 s       | systemd unit state + journal excerpts      |
| ResourcesProbe | every 60 s       | CPU, memory, disk, thermals, network       |
| TimeSyncProbe  | every 300 s      | NTP state, RTC voltage/drift (timedate1 + sysfs) |
| UpdateProbe    | every 1800 s     | RAUC OTA slot and last update result       |

In typical cycles only BootProbe + ServicesProbe + ResourcesProbe run — the ~12 ms burst.
Every ~5 min the TimeSyncProbe adds the NTP/RTC D-Bus query (~62 ms ppoll wait → ~78 ms burst).

### Journal Scanning (0.5.1)

**6 scans per cycle** — 5 monitored services + 1 system-wide JournalProbe. Running as
non-root (UID 986), the daemon gets EACCES on `/run/log/journal/<machine-id>/` and falls
back to `/var/log/journal/`. On this freshly-imaged target (post-RAUC install), the
persistent journal has minimal rotated files — resulting in only ~16 fstat calls across
2 cycles (vs 1,054 in v0.5.0 which had accumulated rotated files).

The sd_journal overhead still scales linearly with rotated file count; this will grow
as the device accumulates uptime. On a long-running device with many rotated files,
expect this to approach the v0.5.0 profile.

---

## vs 0.5.0: What Changed

| Metric                  | 0.5.0          | 0.5.1        | Delta   |
|-------------------------|----------------|--------------|---------|
| Burst wall-clock        | 49 ms          | **12 ms**    | -76%    |
| Total syscalls (2 cyc.) | 1,808          | **338**      | -81%    |
| fstat (2 cyc.)          | 1,054          | **16**       | -98%    |
| Kernel time (2 cyc.)    | 2.66 ms        | **1.93 ms**  | -28%    |
| ppoll blocking          | ~10 ms         | **~0.07 ms** | -99%    |
| D-Bus connections/cycle | 2              | **1**        | -50%    |
| Open FDs (steady state) | 7              | **10**       | +3 (watchdog thread FDs) |
| VmRSS                   | 5.6 MB         | **5.5 MB**   | -100 kB |
| Private dirty           | 524 kB         | **436 kB**   | -88 kB  |
| Duty cycle (normal)     | 0.082%         | **0.020%**   | -76%    |

**Primary driver of burst improvement:** ProbeSchedule (PR #26) — DeviceProbe runs
once only; TimeSyncProbe (NTP + RTC) and UpdateProbe on independent cadences. In
a typical 60s cycle, none of the slow D-Bus probes fire.

**fstat reduction** dominated by journal file count on this fresh target (1 active file,
no rotation) rather than a code change. Expect it to normalize toward v0.5.0 levels as
the device accumulates uptime and journals rotate.

**D-Bus connection reduction:** HealthManager OverallSeverity update now goes through
the long-lived service connection instead of a new ephemeral connection each cycle.

---

## Profile: edge-healthd 0.5.0

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM) running IoT Gateway OS igw.0.1.0,
kernel 6.18.13-v8-16k-igw-00005-g519f4a2b7bf8.

Binary: `edge-healthd 0.5.0` (aarch64, Yocto build, systemd-managed).

### Test Configuration

```json
{
  "collect_interval_sec": 60,
  "time_sync_interval_sec": 300,
  "monitored_services": ["sshd.socket", "NetworkManager.service", "mosquitto.service",
                          "telegraf.service", "influxdb.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": []
}
```

### Collection Cycle Timing

One complete collection burst takes **~49 ms wall-clock**, then the daemon
sleeps for the remainder of the 60 s interval.

| Phase            | Wall-clock | Syscalls | Description                                                         |
|------------------|------------|----------|---------------------------------------------------------------------|
| Device probe     | ~2 ms      | ~10      | `/etc/os-release`, `/proc/device-tree/serial-number`, `/sys/devices/system/cpu/online` |
| Boot probe       | <1 ms      | ~2       | `/data/edge/health/boot_state.json`                                 |
| Journal scan     | ~15 ms     | ~370     | 6 scans × 1 active journal file (5 services + system-wide JournalProbe) |
| D-Bus services   | interleaved| ~110     | query 5 systemd units via `org.freedesktop.systemd1`                |
| Resources        | ~2 ms      | ~25      | `/proc/meminfo`, `/sys/class/thermal`, `sysinfo()`, `statfs`        |
| Netlink + IP     | 0 ms       | 0        | read from in-memory cache                                           |
| NTP check        | **0 ms**   | **0**    | **skipped** — `time_sync_interval_sec=300`, not yet due             |
| D-Bus notify     | ~1 ms      | ~20      | HealthManager property update (ephemeral connection per cycle)      |
| Write snapshot   | <1 ms      | 3        | atomic tmp-write + `renameat` to `state.json`                       |
| **Total burst**  | **~49 ms** | **~904** | then `clock_nanosleep(60 s)`                                        |

### Syscall Summary (130 s window, 2 collection cycles)

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 42.05    0.001118           1      1054           fstat
 10.57    0.000281           3        78        12 openat
  7.45    0.000198           1       156        52 recvmsg
  6.62    0.000176          14        12           munmap
  5.23    0.000139           1        78           close
  4.32    0.000115           1        76           getdents64
  4.14    0.000110           2        50           sendmsg
  3.65    0.000097           1        50           ppoll
  2.37    0.000063           2        22           read
  1.88    0.000050           4        12           mmap
  1.77    0.000047           0        48           fstatfs
  1.65    0.000044           1        42           write
  1.39    0.000037           1        32           getsockopt
  0.98    0.000026           6         4           connect
  0.86    0.000023           2         8           statfs
  0.71    0.000019           4         4           socket
  0.68    0.000018           2         8           eventfd2
  0.68    0.000018           0        24           fcntl
  0.45    0.000012           1         8           setsockopt
  0.34    0.000009           4         2           renameat
  0.30    0.000008           4         2           sysinfo
  0.30    0.000008           2         4           bind
  0.26    0.000007           3         2           writev
  0.26    0.000007           1         4           prctl
  0.19    0.000005           5         1         1 restart_syscall
  0.19    0.000005           1         4           getuid
  0.19    0.000005           1         4           getpeername
  0.15    0.000004           2         2           newfstatat
  0.15    0.000004           1         4           getsockname
  0.15    0.000004           1         4           getrandom
  0.08    0.000002           0         6           uname
  0.00    0.000000           0         1         1 futex
  0.00    0.000000           0         2           getpid
------ ----------- ----------- --------- --------- ----------------
100.00    0.002659           1      1808        66 total
```

**Totals (2 cycles):** 1,808 calls, 2.66 ms kernel time
**Per cycle:** ~904 calls, ~1.33 ms kernel time

### Resource Footprint

| Metric                    | Value                                  |
|---------------------------|----------------------------------------|
| Threads                   | 1 (watchdog thread; systemd-managed)  |
| VmRSS                     | 5.6 MB                                 |
| PSS (proportional set)    | 1.75 MB                                |
| Private dirty memory      | 524 kB                                 |
| Open FDs (steady state)   | 7                                      |
| Total CPU since boot      | 105 ms (`sum_exec_runtime`, longer uptime) |
| Context switches          | 5,884 (5,873 voluntary, 11 involuntary)|
| I/O reads (cumulative)    | 38 KB                                  |
| I/O writes (cumulative)   | 161 KB                                 |
| write_bytes (actual disk) | 4 KB (1 page — snapshot per cycle)    |
| Duty cycle                | 0.082% (49 ms active / 60 s interval) |

### Journal Scanning (0.5.0)

**6 scans per cycle** — 5 monitored services + 1 system-wide JournalProbe. Target has
1 active journal file, no rotation. Each scan opens 4 paths
(`/run/log/journal/`, `/<machine-id>/`, `system.journal`, `/var/log/journal`).

Total: 6 × 4 = 24 `openat` calls for journal, 6 `mmap`/`munmap` pairs, ~94 `fstat` calls/scan (~561 total).

### D-Bus Connection Model (0.5.0)

Two ephemeral D-Bus connections are opened and closed **every cycle**:
1. `org.freedesktop.systemd1` — 5 service queries (GetUnit + unit/service props each)
2. `edge.health.Manager` property update — OverallSeverity publish to D-Bus

Evidence: `socket`/`connect`/`bind`/`eventfd2` appear 4 times each in `strace -c` (2 per cycle).
This is the source of the syscall count increase vs v0.4.0 despite faster wall-clock.
See backlog item: *refactor(dbus): reuse client connection per cycle*.

### NTP / time_sync_interval_sec

`time_sync_interval_sec=300` (default): `org.freedesktop.timedate1` was **not queried** in either
profiled cycle. Zero `ppoll` blocking from NTP. ppoll total across all 23 calls: 10.4 ms
(all non-blocking fast D-Bus round-trips). This is the primary driver of the 49 ms vs 98 ms burst.

When NTP *is* queried (every 5th minute), expect +62 ms burst wall-clock — same cost as v0.4.0.

### Netlink

Zero netlink syscalls at collection time. NetlinkMonitor cache confirmed intact.

---

## vs 0.4.0: What Changed

| Metric                  | 0.4.0          | 0.5.0        | Delta   |
|-------------------------|----------------|--------------|---------|
| Burst wall-clock        | 98 ms          | **49 ms**    | -50%    |
| Total syscalls (2 cyc.) | 1,366          | **1,808**    | +32%    |
| fstat (2 cyc.)          | 708            | **1,054**    | +49%    |
| Kernel time (2 cyc.)    | 3.1 ms         | **2.66 ms**  | -14%    |
| ppoll blocking          | ~62 ms         | **~10 ms**   | -84%    |
| Journal scans/cycle     | 4              | **6**        | +50% (5 services vs 3) |
| Open FDs (steady state) | 8              | **7**        | -1      |
| VmRSS                   | 5.5 MB         | **5.6 MB**   | +100 kB |
| PSS                     | 2.5 MB         | **1.75 MB**  | -30%    |
| Private dirty           | 500 kB         | **524 kB**   | +24 kB  |
| Duty cycle              | 0.16%          | **0.082%**   | -49%    |

**Primary driver of burst improvement:** `time_sync_interval_sec=300` eliminates the
`org.freedesktop.timedate1` D-Bus query from 59 out of every 60 cycles (was ~62 ms ppoll
blocking per cycle in v0.4.0).

**Syscall count increase** is explained entirely by: 2 extra monitored services (2 more journal
scans + 2 more D-Bus service query triples) and the new per-cycle ephemeral D-Bus connection
for HealthManager property publication (~20 syscalls/cycle overhead).

**Kernel time is DOWN** despite more syscalls — the eliminated ppoll waits that were counted
in kernel time previously are now absent.

---

## Profile: edge-healthd 0.4.0

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM) running IoT Gateway OS,
kernel 6.18.13-v8-16k-igw.

Binary: `edge-healthd 0.4.0` (aarch64, Yocto build).

### Test Configuration

```json
{
  "collect_interval_sec": 60,
  "monitored_services": ["sshd.socket", "NetworkManager.service", "mosquitto.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": [],
  "log_excerpt": { "max_lines": 20, "min_priority": "info", "window_sec": 0 }
}
```

### Collection Cycle Timing

One complete collection burst takes **~98 ms wall-clock**, then the daemon
sleeps for the remainder of the 60 s interval via `clock_nanosleep`.

| Phase            | Wall-clock | Syscalls | Description                                              |
|------------------|------------|----------|----------------------------------------------------------|
| Device probe     | ~2 ms      | ~10      | `/etc/os-release`, `/proc/device-tree/serial-number`     |
| Boot probe       | <1 ms      | ~2       | `/data/edge/health/boot_state.json`                      |
| Journal scan     | ~16 ms     | ~300     | 4 scans × 1 active journal file (3 services + system-wide JournalProbe) |
| D-Bus services   | interleaved| ~90      | query 3 systemd units via `org.freedesktop.systemd1`     |
| Resources        | ~2 ms      | ~25      | `/proc/meminfo`, `/sys/class/thermal`, `sysinfo()`, `statfs` |
| Netlink + IP     | 0 ms       | 0        | read from in-memory cache                                |
| NTP check        | ~62 ms     | ~30      | D-Bus to `org.freedesktop.timedate1` (ppoll wait)        |
| Write snapshot   | <1 ms      | 3        | atomic tmp-write + `renameat` to `state.json`            |
| **Total burst**  | **~98 ms** | **~683** | then `clock_nanosleep(60 s)`                             |

### Syscall Summary (130 s window, 2 collection cycles)

```
 30.88%   fstat           708 calls     0.948 ms   <- journal file scanning
 11.14%   openat           54 calls     0.342 ms   <- journal + /proc + /sys
  8.60%   recvmsg         134 calls     0.264 ms   <- D-Bus replies
  6.09%   sendmsg          49 calls     0.187 ms   <- D-Bus requests + journal logging
  5.86%   close            64 calls     0.180 ms
  5.28%   munmap            8 calls     0.162 ms   <- journal cleanup
  4.76%   ppoll            42 calls     0.146 ms   <- D-Bus waits
  4.07%   getdents64       52 calls     0.125 ms   <- journal directory traversal
  2.96%   getsockopt       48 calls     0.091 ms
  2.05%   mmap              8 calls     0.063 ms   <- journal mapping
  ────────────────────────────────────────────────────────
100.00%   TOTAL          1366 calls     3.070 ms kernel time (2 cycles)
```

### Resource Footprint

| Metric                    | Value                                  |
|---------------------------|----------------------------------------|
| Threads                   | 1 (watchdog thread not started — no systemd watchdog in foreground mode) |
| VmRSS                     | 5.5 MB                                 |
| PSS (proportional set)    | 2.5 MB                                 |
| Private dirty memory      | 500 KB                                 |
| Open FDs (steady state)   | 8                                      |
| Total CPU since boot      | 47 ms (`sum_exec_runtime`)             |
| Context switches          | 4,265 (4,258 voluntary, 7 involuntary) |
| I/O reads (cumulative)    | 26 KB                                  |
| I/O writes (cumulative)   | 48 KB                                  |
| Duty cycle                | 0.16% (98 ms active / 60 s interval)   |

### Journal Scanning (0.4.0)

This target has **1 active journal file** (no rotation) under
`/run/log/journal/<machine-id>/system.journal`. Four journal scans run per
cycle: one per monitored service (sshd.socket, NetworkManager.service,
mosquitto.service) plus one system-wide scan from the standalone
`JournalProbe` added in v0.4.0.

Each scan opens:
- `/run/log/journal/` directory
- `/run/log/journal/<machine-id>/` directory
- `/run/log/journal/<machine-id>/system.journal`
- `/var/log/journal/` (checked, not found)

Total: 4 scans × 4 `openat` calls = 16 opens, 4 `mmap`/`munmap` pairs, ~177 `fstat` calls per scan.

> **Important:** The low syscall count compared to the 0.1.0 profile is
> primarily because this target has **1 journal file vs 16 rotated files**
> on the original test system. The `sd_journal` overhead scales linearly
> with the number of rotated files — not with `max_lines` or `window_sec`.

### Netlink and Network Stats

Zero netlink syscalls at collection time — confirmed (`sendto: 0`).
The `NetlinkMonitor` persistent cache serves all network data.

### D-Bus Latency

The `org.freedesktop.timedate1` query accounts for ~62 ms of `ppoll` wait
per cycle — pure latency, not CPU cost.

---

## vs 0.1.0: What Changed

| Metric                  | 0.1.0          | 0.4.0        | Delta   |
|-------------------------|----------------|--------------|---------|
| Burst wall-clock        | 227 ms         | **98 ms**    | -57%    |
| Total syscalls (2 cyc.) | 10,266         | **1,366**    | -87%    |
| `fstat` (2 cyc.)        | 8,822          | **708**      | -92%    |
| `mmap`+`munmap` (2 cyc.)| 256            | **16**       | -94%    |
| `openat` (2 cyc.)       | 180            | **54**       | -70%    |
| `fstatfs` (2 cyc.)      | 144            | **32**       | -78%    |
| Kernel time (2 cyc.)    | 21.4 ms        | **3.1 ms**   | -86%    |
| ppoll blocking          | ~85 ms         | **~68 ms**   | -20%    |
| Netlink syscalls        | 0              | **0**        | —       |
| VmRSS                   | 5.5 MB         | **5.5 MB**   | —       |
| PSS                     | 3.1 MB         | **2.5 MB**   | -19%    |
| Duty cycle              | 0.38%          | **0.16%**    | -58%    |

**Primary driver of improvement:** the 0.4.0 test target has 1 active journal
file vs 16 rotated files on the 0.1.0 test system. Journal overhead scales
with file count. Code changes (JournalProbe system-wide scan, RTM_GETADDR
cache) are also reflected but the journal file count dominates.

**Kernel version change:** 6.6.63 → 6.18.13 may account for minor syscall
timing differences; not isolated.

---

## Profile: edge-healthd 0.1.0 (historical)

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM) running IoT Gateway OS igw.0.1,
kernel 6.6.63-v8-16k-igw.

Binary: `edge-healthd 0.1.0` (aarch64, Yocto SDK build, 12 MB with debug info).

### Test Configuration

```json
{
  "collect_interval_sec": 60,
  "monitored_services": ["sshd.socket", "NetworkManager.service",
                         "rauc.service", "systemd-timesyncd.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": ["eth0", "wlan0"],
  "log_excerpt": { "max_lines": 20, "min_priority": "info", "window_sec": 0 }
}
```

### Collection Cycle Timing

One complete collection burst takes **~227 ms wall-clock**, then the daemon
sleeps for the remainder of the 60 s interval via `clock_nanosleep`.

| Phase            | Wall-clock | Syscalls | Description                                         |
|------------------|------------|----------|-----------------------------------------------------|
| Device probe     | 4 ms       | ~15      | `/etc/machine-id`, `/etc/os-release`, `uname`       |
| Journal scan     | 133 ms     | ~4,970   | open + mmap + fstat scan of 16 rotated journal files |
| D-Bus services   | interleaved| ~86      | query 4 systemd units via `org.freedesktop.systemd1` |
| Resources        | 3 ms       | ~30      | `/proc/meminfo`, `/sys/class/thermal`, `sysinfo()`, `statfs` |
| Netlink + IP     | 0 ms       | 0        | read from in-memory cache (RTM_GETADDR integrated)   |
| NTP check        | 88 ms      | ~30      | D-Bus to `org.freedesktop.timedate1` (85 ms ppoll wait) |
| Write snapshot   | <1 ms      | 3        | atomic tmp-write + `renameat` to `state.json`       |
| **Total burst**  | **~227 ms**| **5,105**| then `clock_nanosleep(60 s)`                        |

### Syscall Summary (130 s window, 2 collection cycles)

```
 75.4%   fstat          8,822 calls    16.2 ms   <- journal file scanning
  4.7%   munmap           128 calls     1.0 ms   <- journal cleanup
  4.2%   openat           180 calls     0.9 ms   <- journal + /proc + /sys
  2.5%   mmap             128 calls     0.5 ms   <- journal mapping (16 files x 4 MB)
  2.0%   fcntl            256 calls     0.4 ms   <- journal fd flags
  2.0%   recvmsg          125 calls     0.4 ms   <- D-Bus replies
  1.3%   fstatfs          144 calls     0.3 ms   <- journal filesystem checks
  1.3%   sendmsg           52 calls     0.3 ms   <- D-Bus requests
  1.0%   ppoll              33 calls     0.2 ms   <- D-Bus waits
  ────────────────────────────────────────────────
100.0%   TOTAL         10,266 calls   21.4 ms kernel time (for 2 cycles)
```

### Resource Footprint

| Metric                    | Value                                  |
|---------------------------|----------------------------------------|
| Threads                   | 1 (single-threaded)                    |
| VmRSS                     | 5.5 MB                                 |
| PSS (proportional set)    | 3.1 MB                                 |
| Private dirty memory      | 768 KB                                 |
| Open FDs (steady state)   | 4 (2 D-Bus sockets, 2 eventfds)        |
| Total CPU since boot      | 45 ms (`sum_exec_runtime`)             |
| Context switches          | 107 (98 voluntary, 9 involuntary)      |
| I/O reads (cumulative)    | 22 KB                                  |
| I/O writes (cumulative)   | 26 KB                                  |
| CPU utilization            | ~0.015% of one core                    |
| Duty cycle                | 0.38% (227 ms active / 60 s interval)  |

### Hotspot: Journal Scanning

The `log_excerpt` feature is responsible for 97% of the per-cycle syscall
count.  The `sd_journal` API opens every rotated journal file under
`/run/log/journal/<machine-id>/`, mmap's each 4 MB file, and performs
thousands of `fstat` calls to traverse the on-disk data structures.

On this device with 16 rotated journal files the cost is:

- 80 `openat` calls (16 files x 4 services, re-opened per service)
- 64 `mmap` / `munmap` pairs (16 files x 4 services)
- ~4,484 `fstat` calls (journal index traversal)
- 72 `fstatfs` calls

#### Mitigation options

| Setting                          | Effect                                |
|----------------------------------|---------------------------------------|
| Reduce `monitored_services`     | Fewer journal scans per cycle         |
| Reduce journal file retention   | Fewer files to open/mmap              |

> **Note:** Setting `"window_sec": 1` and `"max_lines": 1` does **not**
> reduce the journal overhead.  The `sd_journal` library opens and indexes
> all rotated files regardless of the time window — the filtering happens at
> the application layer after the library work is done.

#### A/B test: `log_excerpt` minimised (`max_lines: 1, window_sec: 1`)

Re-profiled with `window_sec: 1` so only the last second of journal entries
is considered.  Results show the `sd_journal` overhead is unchanged:

| Metric               | log_excerpt enabled (20 lines) | log_excerpt minimised (1 line, 1 s) |
|----------------------|-------------------------------|--------------------------------------|
| Burst wall-clock     | 229 ms                        | 218 ms                               |
| Syscalls / cycle     | 5,127                         | 5,157                                |
| Syscalls / 2 cycles  | 10,333                        | 10,330                               |
| fstat calls          | 8,822                         | 8,822                                |
| Kernel time (2 cyc.) | 18.1 ms                       | 23.5 ms                              |
| Journal opens        | 80                            | 80                                   |
| mmap / munmap        | 128                           | 128                                  |

The journal scanning cost is fixed per cycle regardless of `max_lines` or
`window_sec` — it is inherent to the `sd_journal_open` + seek + iterate
pattern used by libsystemd.  To eliminate this overhead, journal collection
must be skipped entirely at the code level (e.g. via an `"enable_log_excerpt": false`
config flag that avoids calling `sd_journal_open`).

### Netlink and Network Stats (0.1.0)

All network data — link state, counters, and IPv4 addresses — is served from
a single persistent `NetlinkMonitor` cache.  **Zero syscalls at collection
time.**

#### NetlinkMonitor (persistent socket)

A `NETLINK_ROUTE` socket is opened once at startup and bound to
`RTMGRP_LINK | RTMGRP_IPV4_IFADDR` for push notifications.  Two initial
dumps populate the in-memory cache:

1. `RTM_GETLINK` — link state, carrier status, `IFLA_STATS64` counters
2. `RTM_GETADDR` (AF_INET) — IPv4 addresses, stored per-ifindex

Runtime events (`RTM_NEWADDR`, `RTM_DELADDR`, `RTM_NEWLINK`, `RTM_DELLINK`)
update the cache as they arrive on the multicast socket.

At collection time, `ResourcesProbe::collect_network()` reads directly from
this cache via `get_all_stats()` — **zero syscalls**.  The persistent socket
costs one FD in steady state.

#### Before/after: `getifaddrs()` elimination

The previous implementation called `get_ipv4_for_interface()` per monitored
interface, which invoked glibc `getifaddrs()` internally.  Each call opened a
new `AF_NETLINK` socket, dumped the full link + address table, and closed it.

| Metric                     | Before (getifaddrs)              | After (RTM_GETADDR cache) |
|----------------------------|----------------------------------|---------------------------|
| Netlink sockets opened     | 2 per cycle                      | 0                         |
| Kernel dumps at collection | 4 (2x GETLINK + 2x GETADDR)     | 0                         |
| `sendto` calls             | 4                                | 0                         |
| `recvmsg` calls            | ~10                              | 0                         |
| Data received              | ~9.8 KB                          | 0                         |
| Wall-clock per cycle       | **1.6 ms**                       | **0 ms**                  |
| Per-cycle syscalls          | ~22                              | 0                         |

Verified via `strace -c` over 130 s: zero `sendto` calls and zero
`AF_NETLINK` socket opens during collection cycles.

---

## Profiling Methodology

### Target

| Property          | Value                                                 |
|-------------------|-------------------------------------------------------|
| Board             | Raspberry Pi 5 (BCM2712, 4x Cortex-A76 @ 2.4 GHz)   |
| RAM               | 8 GB LPDDR4X                                          |
| OS                | IoT Gateway OS                                        |
| Access            | SSH                                                   |

### Benchmark storage

Store all strace output on the target under `/data/edge-healthd-benchmark/`
(persistent across sessions — `/tmp` is a hardened tmpfs, cleaned per SSH session).

### Daemon launch

```sh
systemctl stop edge-healthd
nohup edge-healthd -f -v -c /etc/edge/healthd.conf \
  > /data/edge-healthd-benchmark/daemon.log 2>&1 &
sleep 8 && journalctl _PID=$(pgrep edge-healthd) -n 10 --no-pager
```

### Step 1: Syscall summary (`strace -c`)

```sh
PID=$(pgrep edge-healthd)
strace -c -p $PID 2>&1 &
STRACE_PID=$!
sleep 130
kill $STRACE_PID
wait $STRACE_PID 2>/dev/null
```

### Step 2: Timestamped trace (`strace -T -tt`)

```sh
PID=$(pgrep edge-healthd)
timeout 75 strace -T -tt -p $PID -o /data/edge-healthd-benchmark/strace.log
echo "Lines: $(wc -l < /data/edge-healthd-benchmark/strace.log)"
```

### Step 3: Process statistics from `/proc`

```sh
PID=$(pgrep edge-healthd)
echo "=== status ===" && cat /proc/$PID/status
echo "=== smaps_rollup ===" && cat /proc/$PID/smaps_rollup
echo "=== sched ===" && cat /proc/$PID/sched
echo "=== io ===" && cat /proc/$PID/io
echo "=== fds ===" && ls -la /proc/$PID/fd/
```

### Step 4: Analysing the trace

```sh
LOG=/data/edge-healthd-benchmark/strace.log

echo "--- syscall counts ---"
for sc in fstat openat mmap munmap sendmsg recvmsg ppoll sendto getdents64 fstatfs; do
  echo "$sc: $(grep -c $sc $LOG)"
done

echo "--- total kernel time ---"
grep -oP '<\K[0-9.]+(?=>)' $LOG | awk '{sum+=$1} END {printf "%.6f sec\n", sum}'

echo "--- ppoll blocking time ---"
grep 'ppoll' $LOG | grep -oP '<\K[0-9.]+(?=>)' | awk '{sum+=$1; n++} END {printf "%d calls, %.6f sec\n", n, sum}'

echo "--- burst wall-clock ---"
sed -n '2p' $LOG | awk '{print "start:", $1}'
tail -2 $LOG | head -1 | awk '{print "end:  ", $1}'

echo "--- files opened ---"
grep 'openat.*= [0-9]' $LOG | sed 's/.*openat[^"]*"//' | sed 's/".*//' \
  | sort | uniq -c | sort -rn | head -20

echo "--- netlink check ---"
grep -c 'AF_NETLINK\|sendto' $LOG || echo 0
```

### Reproducing on a different platform

1. Deploy the binary and config to the target.
2. Start the daemon with `-f -v -c /etc/edge/healthd.conf`.
3. Wait for at least one collection cycle (`journalctl _PID=$(pgrep edge-healthd) -n 5`).
4. Run Steps 1–4 above in order.
5. Record the target hardware, OS, kernel, and binary version in the report.
