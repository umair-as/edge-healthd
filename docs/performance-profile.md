# edge-healthd Performance Profile

Profiled on Raspberry Pi 5 (4-core Cortex-A76, 8 GB RAM) running IoT Gateway OS igw.0.1,
kernel 6.6.63-v8-16k-igw.

Binary: `edge-healthd 0.1.0` (aarch64, Yocto SDK build, 12 MB with debug info).

## Test Configuration

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

## Collection Cycle Timing

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

## Syscall Summary (130 s window, 2 collection cycles)

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

## Resource Footprint

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

## Hotspot: Journal Scanning

The `log_excerpt` feature is responsible for 97% of the per-cycle syscall
count.  The `sd_journal` API opens every rotated journal file under
`/run/log/journal/<machine-id>/`, mmap's each 4 MB file, and performs
thousands of `fstat` calls to traverse the on-disk data structures.

On this device with 16 rotated journal files the cost is:

- 80 `openat` calls (16 files x 4 services, re-opened per service)
- 64 `mmap` / `munmap` pairs (16 files x 4 services)
- ~4,484 `fstat` calls (journal index traversal)
- 72 `fstatfs` calls

### Mitigation options

| Setting                          | Effect                                |
|----------------------------------|---------------------------------------|
| Reduce `monitored_services`     | Fewer journal scans per cycle         |
| Reduce journal file retention   | Fewer files to open/mmap              |

> **Note:** Setting `"window_sec": 1` and `"max_lines": 1` does **not**
> reduce the journal overhead.  The `sd_journal` library opens and indexes
> all rotated files regardless of the time window — the filtering happens at
> the application layer after the library work is done.

### A/B test: `log_excerpt` minimised (`max_lines: 1, window_sec: 1`)

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

## Netlink and Network Stats

All network data — link state, counters, and IPv4 addresses — is served from
a single persistent `NetlinkMonitor` cache.  **Zero syscalls at collection
time.**

### NetlinkMonitor (persistent socket)

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

### Before/after: `getifaddrs()` elimination

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

## D-Bus Latency

Two D-Bus connections are opened per cycle (one for systemd1, one for
timedated).  The `org.freedesktop.timedate1` query accounts for 85 ms of
blocking `ppoll` wait, which is pure latency and not CPU cost.

## Profiling Methodology

### Target

| Property          | Value                                                 |
|-------------------|-------------------------------------------------------|
| Board             | Raspberry Pi 5 (BCM2712, 4x Cortex-A76 @ 2.4 GHz)   |
| RAM               | 8 GB LPDDR4X                                          |
| OS                | IoT Gateway OS igw.0.1                                |
| Kernel            | 6.6.63-v8-16k-igw (aarch64, 16K pages)               |
| Binary            | `edge-healthd 0.1.0` (Yocto SDK cross-build, aarch64)|
| Binary size       | 12 MB (with debug info, not stripped)                 |
| Access            | `ssh root@192.168.0.82`                               |

### Daemon launch

The daemon was started in foreground/verbose mode with an explicit config:

```sh
# On the target device:
nohup /data/edge/edge-healthd-wrapper -f -v \
  -c /data/etc/edge/healthd.conf > /tmp/edge-healthd.log 2>&1 &

# Verify:
pgrep -a edge-healthd
# 196199 /data/edge/edge-healthd -f -v -c /data/etc/edge/healthd.conf
```

The wrapper script sets `LD_LIBRARY_PATH` for Yocto sysroot libraries:

```sh
#!/bin/sh
export LD_LIBRARY_PATH=/data/lib:/usr/lib:/lib:$LD_LIBRARY_PATH
exec /data/edge/edge-healthd "$@"
```

### Step 1: Syscall summary (`strace -c`)

Captures aggregate syscall counts and kernel time over 130 seconds
(≥ 2 collection cycles at 60 s interval):

```sh
PID=$(pgrep edge-healthd)

# Attach strace in summary mode, let it run for 130 s, then detach.
strace -c -p $PID -f 2>&1 &
STRACE_PID=$!
sleep 130
kill $STRACE_PID
wait $STRACE_PID 2>/dev/null
```

Output is the `% time / seconds / usecs/call / calls / errors / syscall`
table used in the "Syscall Summary" section above.

### Step 2: Timestamped trace (`strace -T -tt`)

Captures every syscall with wall-clock timestamps and per-call duration,
written to a file for offline analysis:

```sh
PID=$(pgrep edge-healthd)

# Capture ~75 s (one full collection cycle + sleep transition).
timeout 75 strace -T -tt -p $PID -o /tmp/edge-healthd-strace.log

# Verify line count (expect ~5,100 lines per cycle):
wc -l /tmp/edge-healthd-strace.log
```

- `-T` appends `<elapsed>` in seconds to each line (e.g. `<0.000032>`)
- `-tt` prefixes each line with `HH:MM:SS.usec` wall-clock time

**Important:** Only one tracer can attach to a process at a time.  Steps 1
and 2 must be run sequentially, not concurrently.

### Step 3: Process statistics from `/proc`

Collected while the daemon was running (not under strace, to avoid
perturbation):

```sh
PID=$(pgrep edge-healthd)

# Memory: RSS, PSS, private/shared dirty, swap
cat /proc/$PID/status
cat /proc/$PID/smaps_rollup

# Scheduling: total CPU runtime, context switches, vruntime
cat /proc/$PID/sched

# I/O: cumulative bytes read/written, syscall counts
cat /proc/$PID/io

# Open file descriptors
ls -la /proc/$PID/fd/

# Thread count
ls /proc/$PID/task/
```

### Step 4: Analysing the trace

The raw trace file is analysed with standard command-line tools:

```sh
LOG=/tmp/edge-healthd-strace.log

# --- Collection burst wall-clock ---
# First syscall after wakeup (line 2, line 1 is the sleep return):
sed -n '2p' $LOG | cut -d' ' -f2
# Last syscall before next sleep:
tail -2 $LOG | head -1 | cut -d' ' -f2

# --- Syscall counts by category ---
grep -c 'fstat'      $LOG   # journal scanning
grep -c 'openat'     $LOG   # file/journal opens
grep -c 'mmap'       $LOG   # journal mapping
grep -c 'sendmsg'    $LOG   # D-Bus requests
grep -c 'recvmsg'    $LOG   # D-Bus replies
grep -c 'ppoll'      $LOG   # D-Bus waits
grep -c 'sendto'     $LOG   # netlink requests

# --- Files opened during the cycle ---
grep 'openat.*= [0-9]' $LOG \
  | sed 's/.*openat[^"]*"//' | sed 's/".*//' \
  | sort | uniq -c | sort -rn

# --- Total kernel time (sum of all <elapsed> values) ---
grep -oP '<\K[0-9.]+(?=>)' $LOG \
  | awk '{sum+=$1} END {printf "%.6f sec\n", sum}'

# --- D-Bus ppoll blocking time ---
grep 'ppoll' $LOG | grep -oP '<\K[0-9.]+(?=>)' \
  | awk '{sum+=$1; n++} END {printf "%d calls, %.6f sec\n", n, sum}'

# --- Netlink socket sequence ---
grep -n -E 'socket\(AF_NETLINK|sendto.*RTM_GET|recvmsg.*NETLINK|bind.*AF_NETLINK' $LOG

# --- Phase boundaries ---
grep -n 'machine-id\|os-release\|boot_state\|journal\|sysinfo\|meminfo\|thermal\|timedate1\|state.json.tmp\|renameat\|clock_nanosleep' $LOG
```

### Reproducing on a different platform

1. Deploy the binary and config to the target.
2. Start the daemon with `-f -v -c <config>`.
3. Wait for at least one collection cycle to complete (check `state.json`).
4. Run Steps 1–4 above in order.
5. Record the target hardware, OS, kernel, and binary version in the report.

Ensure `strace` is available on the target (`strace --version`).  On Yocto
images it may need to be added to `IMAGE_INSTALL` or copied from the SDK
sysroot.
