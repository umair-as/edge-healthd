# Usage Guide

This document covers running and operating `edge-healthd` in production. For build instructions and contributor workflow, see [`development.md`](development.md). For the snapshot schema contract, see [`edge.health.state.v1.0.md`](edge.health.state.v1.0.md).

## Contents

- [Installation](#installation)
- [CLI](#cli)
- [Configuration](#configuration)
- [Snapshot output](#snapshot-output)
- [D-Bus interface](#d-bus-interface)
- [Optional web UI](#optional-web-ui)
- [Operations](#operations)
- [Deployment notes](#deployment-notes)

## Installation

```bash
# After building (see development.md)
sudo cmake --install build

# Enable and start
sudo systemctl enable --now edge-healthd

# Verify
systemctl status edge-healthd
```

The systemd unit installs to `/usr/lib/systemd/system/edge-healthd.service` and runs as an unprivileged `edgehealth` user. The `/run/health/` runtime directory is created automatically via `RuntimeDirectory=health`.

## CLI

```bash
edge-healthd                         # Run as daemon (systemd-managed)
edge-healthd -f -v                   # Foreground with verbose logging
edge-healthd --once                  # Single snapshot then exit
edge-healthd -c /path/to/conf.json   # Custom config file
edge-healthd --dump-config           # Print effective config and exit
edge-healthd --version               # Print version (from git describe)
```

## Configuration

Default config path: `/etc/edge/healthd.conf` (JSON with `//` line comments). Every field is optional — the daemon runs with built-in defaults.

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

### Key defaults

- `device_id` — auto-detected from `/proc/device-tree/serial-number`, then `/etc/machine-id`
- `monitored_interfaces` — when empty, all non-loopback interfaces are reported automatically
- `snapshot_file` — `/run/health/state.json` (tmpfs)
- `state_dir` — `/data/edge/health` (persistent boot/update state; survives reboots)

### Polling intervals and scheduling

- `collect_interval_sec` — master cadence; per-cycle probes (services, resources, journal, crash) run at this rate
- `time_sync_interval_sec` — independent interval for `timedated` and RTC sysfs; avoids socket-activation churn
- `update_check_interval_sec` — independent interval for RAUC D-Bus (OTA state changes are rare); a `RAUC.Installer.Completed` signal also forces an immediate refresh
- `trigger_min_interval_sec` — minimum gap between accepted `TriggerSnapshot` D-Bus calls
- Device identity (OS release, machine-id, arch) is read once at startup and cached

> The `Snapshot collected` journal log line is rate-limited to once every 5 minutes when severity is unchanged. To infer actual cadence, use the monotonic `cycle` counter in the snapshot or the D-Bus interface.

See [`config/healthd.conf.example`](../config/healthd.conf.example) for the fully annotated reference. Validate a snapshot against the schema:

```bash
python3 scripts/validate_schema.py /run/health/state.json
```

## Snapshot output

Written to `/run/health/state.json` (tmpfs) every collection cycle. Schema version `1.0` — see [`schemas/edge.health.state.v1.0.json`](../schemas/edge.health.state.v1.0.json) and the [field reference](edge.health.state.v1.0.md).

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

### Severity levels

| Severity | Meaning |
|----------|---------|
| 🟢 `ok` | All monitored subsystems healthy |
| 🟡 `warn` | Degraded — service restarting, disk > 80 %, NTP unlocked, recent journal errors |
| 🔴 `crit` | Action required — service failed, disk > 95 %, boot loop, kernel panic detected |
| ⚪ `unknown` | Probe could not collect data (D-Bus unavailable, etc.) |

The top-level `summary.severity` is the worst-wins aggregate across subsystems. `summary.reasons` lists which probes drove the result, as stable machine-readable codes (see [schema contract §5](edge.health.state.v1.0.md#5-reason-codes-summaryreasons)).

## D-Bus interface

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

D-Bus policy: [`config/edge-healthd-dbus.conf`](../config/edge-healthd-dbus.conf). Introspection XML: [`dbus/edge-health-manager.xml`](../dbus/edge-health-manager.xml).

`TriggerSnapshot` is rate-limited to one call per `trigger_min_interval_sec` (default 5 s) to prevent local clients from driving continuous collection cycles.

### Example calls

```bash
# Read severity property
busctl get-property edge.health /edge/health/manager edge.health.Manager OverallSeverity

# Force an immediate snapshot
busctl call edge.health /edge/health/manager edge.health.Manager TriggerSnapshot

# Pull cached recent error lines (up to 50)
busctl call edge.health /edge/health/manager edge.health.Manager GetRecentLogs u 50

# Watch for HealthAlarm signals
busctl monitor edge.health
```

## Optional web UI

A small Go server with an embedded Preact + TypeScript dashboard ships behind `EDGE_WEB_UI=ON`. Seven views: Dashboard, Services, Resources, Network, TimeSync, Update, Journal. HTTPS-first (bring your own cert via `-tls-cert` / `-tls-key`; a helper script generates a self-signed RSA-4096 cert with SAN). WebSocket push with HTTP-polling fallback and `localStorage` persistence.

See [`web/README.md`](../web/README.md) for server flags, TLS setup, and the CSRF / same-origin model.

## Operations

```bash
# Check status (live severity is in the StatusText line)
systemctl status edge-healthd

# Live log tail
journalctl -u edge-healthd -f

# Inspect the latest snapshot
cat /run/health/state.json | jq .summary
cat /run/health/state.json | jq '.services.units[] | select(.severity != "ok")'

# Trigger a fresh cycle via D-Bus (rate-limited)
busctl call edge.health /edge/health/manager edge.health.Manager TriggerSnapshot

# Dump effective config without starting the daemon
edge-healthd --dump-config
```

The persistent boot-failure counter lives at `/data/edge/health/boot_state.json`. It's the only file the daemon writes outside tmpfs.

## Deployment notes

- **Read-only rootfs.** Install the D-Bus policy file at `/etc/dbus-1/system.d/edge-healthd-dbus.conf` (or let the build do it). Ensure `/data/edge/health/` is on a writable filesystem.
- **Unprivileged service account.** The unit drops to user/group `edgehealth` with `CapabilityBoundingSet=CAP_NET_RAW` (required for `NETLINK_ROUTE`). Everything else is restricted via `ProtectSystem=strict`, `NoNewPrivileges`, syscall filter, `MemoryDenyWriteExecute`.
- **Resource caps.** The unit ships with `MemoryMax=64M` and `CPUQuota=10%`. Adjust via a systemd drop-in if your gateway has very different limits.
- **Watchdog.** `WatchdogSec=150` — long enough to absorb a slow NTP cycle (~78 ms burst) without false reset. If you change `collect_interval_sec` above ~75 s, increase `WatchdogSec` proportionally.
- **First boot.** `boot_ok` flips to `true` only after the first successful collection cycle completes and `sd_notify(READY=1)` has fired.

For benchmark methodology and historical traces, see [`performance-profile.md`](performance-profile.md). For the snapshot schema contract and evolution rules, see [`edge.health.state.v1.0.md`](edge.health.state.v1.0.md).
