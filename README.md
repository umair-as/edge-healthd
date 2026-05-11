# edge-healthd 🩺

[![CI](https://github.com/umair-as/edge-healthd/actions/workflows/ci.yml/badge.svg)](https://github.com/umair-as/edge-healthd/actions/workflows/ci.yml)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-linux-lightgrey.svg)]()

A lightweight, offline-first health monitoring daemon for resource-constrained edge gateways. Modular probes feed an aggregator that writes an atomic JSON snapshot to tmpfs each cycle — designed for fleet observability on devices that are intermittently connected, read-only-rootfs, and flash-wear sensitive.

**Why edge-healthd?**

- **Tiny footprint** — ~12 ms / 0.97 ms kernel time per cycle; ~0.02 % duty cycle on a Raspberry Pi 5. Capped at 64 MB RAM / 10 % CPU via systemd.
- **No agent server required** — writes a single JSON file. Read it locally, scrape it, or expose it via the optional web UI.
- **Flash-friendly** — snapshot lives in tmpfs; persistent state is one tiny file under `/data`.
- **Hardened** — `ProtectSystem=strict`, `NoNewPrivileges`, syscall filter, dropped to a single capability (`CAP_NET_RAW`).
- **Built for offline** — gracefully degrades when D-Bus services (RAUC, timedated) are absent.

## 📋 Table of Contents

- [Supported platforms](#-supported-platforms)
- [Features](#-features)
- [Architecture](#-architecture)
- [Snapshot output](#-snapshot-output)
- [D-Bus interface](#-d-bus-interface)
- [Optional web UI](#-optional-web-ui)
- [Building](#-building)
- [Usage](#-usage)
- [Configuration](#-configuration)
- [Deployment](#-deployment)
- [Performance](#-performance)
- [Contributing](#-contributing)
- [License](#-license)

## 🖥️ Supported Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| Raspberry Pi 5 | aarch64 | ✅ Production (Yocto `igw.0.1`) |
| VisionFive2 | riscv64 | 🔨 build-tested |
| i.MX93 EVK | aarch64 | 🔨 build-tested |

Targets any Linux ≥ 5.10 with systemd ≥ 250. The probe layer degrades cleanly on systems missing optional services (RAUC, timedated, journald).

## ✨ Features

Eight probes feeding a single aggregator:

- 🔁 **Boot health** — uptime, consecutive boot-failure tracking via persistent state
- ⚙️ **Systemd services** — unit states, restart counts, socket-activated service detection (e.g. `sshd.socket`)
- 📊 **System resources** — CPU load, memory, disk mounts, temperature, network stats via event-driven Netlink (zero-poll)
- 🌐 **Network auto-discovery** — reports all non-loopback interfaces when `monitored_interfaces` is unset
- 🕐 **Time synchronization** — NTP lock state via `org.freedesktop.timedate1`; RTC battery voltage and clock drift via sysfs
- 📦 **OTA updates** — RAUC A/B slot status, bundle version, install timestamp via `de.pengutronix.rauc` D-Bus; immediate refresh on `Installer.Completed`
- 📓 **Journal scan** — system-wide error/critical log digest with bounded scan budget and in-memory cache
- 💥 **Crash artifacts** — kernel-panic detection via `systemd-pstore`, FNV-1a fingerprinting, alarm-once semantics
- 🔖 **Device identity** — stable hardware ID from `/proc/device-tree/serial-number` (survives re-images)

Plus:

- ⚛️ **Atomic snapshot writes** — temp file + `fsync` + `rename`; readers never see partial writes
- 💾 **Flash-wear reduction** — snapshot on tmpfs (`/run/health/state.json`); persistent state on `/data`
- 🔒 **Systemd hardening** — `Type=notify`, watchdog heartbeat, `ProtectSystem=strict`, `NoNewPrivileges`, syscall filter, single ambient capability
- 📡 **D-Bus interface** — `edge.health.Manager` exposes severity, on-demand collection, recent-error retrieval, and `HealthAlarm` signal
- 🌐 **Optional web UI** — Go server + Preact dashboard, opt-in via `EDGE_WEB_UI=ON`

## 🏗️ Architecture

```mermaid
graph TD
    subgraph Probes
        DP[DeviceProbe]
        BP[BootProbe]
        SP[ServicesProbe]
        RP[ResourcesProbe]
        TP[TimeSyncProbe]
        UP[UpdateProbe]
        JP[JournalProbe]
        CP[CrashProbe]
    end

    NL[NetlinkMonitor] --> RP
    RAUC[RAUC D-Bus] --> UP
    SD[systemd D-Bus] --> SP
    TD[timedate1 D-Bus] --> TP
    JD[sd_journal] --> JP
    PS[/var/lib/systemd/pstore] --> CP

    DP --> AGG[Aggregator]
    BP --> AGG
    SP --> AGG
    RP --> AGG
    TP --> AGG
    UP --> AGG
    JP --> AGG
    CP --> AGG

    AGG --> W[Writer]
    W --> OUT[/run/health/state.json on tmpfs]
    AGG --> DBUS[edge.health.Manager D-Bus]

    DAEMON[SnapshotDaemon] -.->|sd-notify + watchdog| AGG
```

Each probe implements a `collect()` returning `std::expected<DataType, ProbeError>`. The `SnapshotAggregator` runs them on independent schedules (master cadence + decoupled NTP/RAUC intervals), evaluates per-subsystem severity thresholds, and emits a `SnapshotState`. The writer commits it atomically.

## 📄 Snapshot Output

Written to `/run/health/state.json` (tmpfs) every collection cycle. Schema version `1.0` — see [`schemas/edge.health.state.v1.0.json`](schemas/edge.health.state.v1.0.json) and the [field reference](docs/edge.health.state.v1.0.md).

```json
{
  "schema": "edge.health.state",
  "schema_version": "1.0",
  "generated_at": "2026-05-11T10:34:00Z",
  "cycle": 4218,
  "device": {
    "device_id": "a9c9b3afbe71fc40",
    "hostname": "iot-gateway",
    "platform": "rpi5",
    "arch": "aarch64",
    "os": { "distro": "iotgw", "version": "igw.0.1", "kernel": "6.18.13-v8-16k-igw" }
  },
  "boot": { "boot_ok": true, "uptime": 3600, "boot_fail_count": 0 },
  "services": {
    "overall": "ok",
    "units": [
      { "name": "sshd.service", "state": "active", "result": "socket-activated", "severity": "ok" },
      { "name": "mosquitto.service", "state": "active", "severity": "ok" }
    ]
  },
  "resources": {
    "cpu": { "load1": 0.0, "load5": 0.0, "load15": 0.05 },
    "memory": { "mem_total_mb": 7923, "mem_used_mb": 201, "swap_used_mb": 0 },
    "storage": [{ "mount": "/", "fs": "ext4", "used_pct": 41, "avail_mb": 1623 }],
    "network": [
      { "ifname": "wlan0", "link": "up", "ip": "192.168.28.50", "rx_bytes": 317020 },
      { "ifname": "br0",   "link": "up", "ip": "192.168.0.82",  "rx_bytes": 34168369 }
    ]
  },
  "time_sync": {
    "overall": "ok", "source": "ntp",
    "ntp": { "enabled": true, "state": "locked", "last_sync_at": "2026-04-09T10:00:00Z" },
    "ptp": { "enabled": false },
    "rtc": { "enabled": true, "hctosys": true, "voltage_mv": 2999, "drift_sec": 1 }
  },
  "update": {
    "overall": "ok",
    "active_slot": "B",
    "last_update": {
      "id": "r0/20260303085902",
      "installed_at": "2026-03-03T09:19:39Z",
      "result": "success",
      "detail": "iot-gateway-raspberrypi5"
    }
  },
  "journal": { "overall": "ok", "error_count": 0, "recent_errors": [] },
  "crash": { "present": false, "artifact_count": 0, "acknowledged": false },
  "summary": { "severity": "ok", "reasons": [] }
}
```

### 🚦 Severity levels

| Severity | Meaning |
|----------|---------|
| 🟢 `ok` | All monitored subsystems healthy |
| 🟡 `warn` | Degraded — service restarting, disk > 80 %, NTP unlocked, recent journal errors |
| 🔴 `crit` | Action required — service failed, disk > 95 %, boot loop, kernel panic detected |
| ⚪ `unknown` | Probe could not collect data (D-Bus unavailable, etc.) |

The top-level `summary.severity` is the max across subsystems. `summary.reasons` lists which probes drove the result.

## 📡 D-Bus Interface

`edge-healthd` exposes a system-bus service for local consumers (alarm panels, operator tools, the bundled web UI):

- **Bus name:** `edge.health`
- **Object:** `/edge/health/manager`
- **Interface:** `edge.health.Manager`

| Member | Kind | Signature | Purpose |
|---|---|---|---|
| `OverallSeverity` | property (emits-change) | `s` | `"ok"` / `"warn"` / `"crit"` / `"unknown"` |
| `TriggerSnapshot` | method | `() → b` | Wake collection immediately; `false` if rate-limited |
| `GetRecentLogs` | method | `(u max_lines) → as` | Cached journal errors — zero `sd_journal_open()` overhead |
| `HealthAlarm` | signal | `(s component, s message, s severity)` | Fired on severity degradation or new crash artifact |

D-Bus policy: [`config/edge-healthd-dbus.conf`](config/edge-healthd-dbus.conf). Introspection XML: [`dbus/edge-health-manager.xml`](dbus/edge-health-manager.xml).

`TriggerSnapshot` is rate-limited to one call per `trigger_min_interval_sec` (default 5 s) to prevent local clients from driving continuous collection cycles.

## 🌐 Optional Web UI

A small Go server with an embedded Preact + TypeScript dashboard ships behind `EDGE_WEB_UI=ON`. Six views: Dashboard, Services, Resources, Network, TimeSync, Update, Journal. HTTPS-first (bring your own cert via `-tls-cert` / `-tls-key`; a helper script generates a self-signed RSA-4096 cert with SAN). WebSocket push with HTTP-polling fallback and localStorage persistence.

See [`web/README.md`](web/README.md) for details.

## 🔨 Building

### Native (development)

Requirements: cmake 3.20+, ninja, GCC 13+ (or Clang 17+), libsystemd-dev, libmnl-dev, pkg-config.

```bash
# Debug build with tests and sanitizers
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DEDGE_BUILD_TESTS=ON -DEDGE_ENABLE_SANITIZERS=ON \
  -DEDGE_FETCH_SDBUSCPP=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDGE_FETCH_SDBUSCPP=ON
cmake --build build
```

### Cross-compile with a Yocto SDK

```bash
# Source the SDK environment (sets CC, CXX, sysroot, etc.)
source /path/to/sdk/environment-setup-cortexa76-oe-linux   # RPi5 / i.MX93
# source /path/to/sdk/environment-setup-riscv64-oe-linux   # VisionFive2

cmake -B build-target -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DEDGE_FETCH_SDBUSCPP=ON \
  -DCMAKE_TOOLCHAIN_FILE=$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake
cmake --build build-target
```

> **Note (cross-builds):** when `EDGE_FETCH_SDBUSCPP=ON`, `sdbus-c++-xml2cpp` is compiled for the target. The D-Bus adaptor header is therefore pre-generated and committed at `inc/generated/HealthManagerAdaptor.hpp`. Regenerate via a native build (`cmake --build build --target edge-dbus-generate`) if you change [`dbus/edge-health-manager.xml`](dbus/edge-health-manager.xml).

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_BUILD_TESTS` | OFF | Build Catch2 unit tests |
| `EDGE_ENABLE_SANITIZERS` | OFF | ASan + UBSan (Debug builds only) |
| `EDGE_ENABLE_LTO` | ON | Link-time optimization (Release builds) |
| `EDGE_FETCH_SDBUSCPP` | OFF | Auto-fetch sdbus-c++ v2.0 via FetchContent |
| `EDGE_WEB_UI` | OFF | Build optional Go + Preact web dashboard |

## 🚀 Usage

```bash
edge-healthd                         # Run as daemon (systemd-managed)
edge-healthd -f -v                   # Foreground with verbose logging
edge-healthd --once                  # Single snapshot then exit
edge-healthd -c /path/to/conf.json   # Custom config file
edge-healthd --dump-config           # Print effective config and exit
```

## ⚙️ Configuration

Default config path: `/etc/edge/healthd.conf` (JSON, with `//` line comments). Every field is optional — the daemon runs with built-in defaults.

```json
{
  "device_id": "",
  "platform": "rpi5",
  "collect_interval_sec": 60,
  "time_sync_interval_sec": 300,
  "update_check_interval_sec": 1800,
  "trigger_min_interval_sec": 5,
  "monitored_services": ["sshd.socket", "NetworkManager.service", "mosquitto.service"],
  "monitored_mounts": ["/", "/data"],
  "monitored_interfaces": [],
  "enable_ntp": true,
  "enable_ptp": false,
  "enable_rtc": true,
  "rtc_device": "/sys/class/rtc/rtc0",
  "enable_thermal": true,
  "enable_update_tracking": true,
  "thresholds": {
    "cpu_load_warn": 80,  "cpu_load_crit": 95,
    "mem_used_warn": 80,  "mem_used_crit": 95,
    "disk_used_warn": 80, "disk_used_crit": 95,
    "temp_warn_c": 70.0,  "temp_crit_c": 85.0,
    "service_restart_warn": 3, "service_restart_crit": 10,
    "boot_fail_warn": 1,  "boot_fail_crit": 3,
    "rtc_voltage_warn_mv": 2700, "rtc_voltage_crit_mv": 2500
  }
}
```

**Key defaults:**
- `device_id` — auto-detected from `/proc/device-tree/serial-number`, then `/etc/machine-id`
- `monitored_interfaces` — when empty, all non-loopback interfaces are reported automatically
- `snapshot_file` — `/run/health/state.json` (tmpfs)
- `state_dir` — `/data/edge/health` (persistent boot/update state; survives reboots)

**Polling intervals and scheduling:**
- `collect_interval_sec` — master cadence; per-cycle probes (services, resources, journal, crash) run at this rate
- `time_sync_interval_sec` — independent interval for `timedated` and RTC sysfs; avoids socket-activation churn
- `update_check_interval_sec` — independent interval for RAUC D-Bus (OTA state changes are rare); a `RAUC.Installer.Completed` signal also forces an immediate refresh
- `trigger_min_interval_sec` — minimum gap between accepted `TriggerSnapshot` D-Bus calls
- Device identity (OS release, machine-id, arch) is read once at startup and cached

> **Note:** the `Snapshot collected` journal log line is rate-limited to once every 5 minutes when severity is unchanged. This is intentional — to infer actual cadence, use the monotonic `cycle` counter in the snapshot or the D-Bus interface.

See [`config/healthd.conf.example`](config/healthd.conf.example) for the full annotated reference.

Validate a snapshot against the schema:

```bash
python3 scripts/validate_schema.py /run/health/state.json
```

## 📡 Deployment

```bash
# Install binary and service unit
sudo cmake --install build

# Enable and start
sudo systemctl enable --now edge-healthd

# Check status
systemctl status edge-healthd
journalctl -u edge-healthd -f

# Read snapshot
cat /run/health/state.json | jq .summary
```

The service unit runs as an unprivileged `edgehealth` user. The `/run/health` directory is created automatically via `RuntimeDirectory=health`. On read-only-rootfs targets, install the D-Bus policy file at `/etc/dbus-1/system.d/edge-healthd-dbus.conf` and ensure `/data/edge/health` is writable.

## 📊 Performance

Reference numbers from a Raspberry Pi 5 running `iotgw igw.0.1`:

| Phase | Wall time | Syscalls | Kernel time | Frequency |
|---|---|---|---|---|
| Normal burst | ~12 ms | 169 | 0.97 ms | every `collect_interval_sec` (60 s) |
| NTP cycle burst | ~78 ms | — | — | every `time_sync_interval_sec` (300 s) |
| Duty cycle | 0.020 % normal / 0.130 % NTP | | | |

D-Bus connection count is 1 per cycle (long-lived service connection shared across all probes). Full methodology and traces: [`docs/performance-profile.md`](docs/performance-profile.md).

## 🤝 Contributing

Issues and pull requests are welcome.

- **Conventional Commits**: `type(scope): description` (e.g. `fix(netlink): ...`, `feat(probes): ...`)
- Types: `feat`, `fix`, `refactor`, `build`, `docs`, `test`, `chore`, `perf`
- One commit per feature/fix — PRs are squash-merged onto `main`
- Run `ctest --test-dir build --output-on-failure` before submitting
- Sanitizer-clean (ASan + UBSan) is a hard CI requirement

For larger changes, please open an issue first to discuss scope.

## 📜 License

[MIT](LICENSE) — Copyright (c) 2026 Umair A.S.
