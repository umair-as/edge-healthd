# edge-healthd Performance Profile

## Profiles

| Version | Board | Kernel | Date | Burst | Syscalls/cycle |
|---------|-------|--------|------|-------|----------------|
| 0.1.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.6.63-v8-16k-igw | 2026-02-xx | 227 ms | 5,133 |
| 0.4.0   | Raspberry Pi 5 (BCM2712, 4× Cortex-A76 @ 2.4 GHz) | 6.18.13-v8-16k-igw | 2026-03-03 | **98 ms** | **683** |

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
| Access            | `ssh iotgw`                                           |

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
