# edge-healthd

Edge device health monitoring daemon for embedded Linux platforms.

## Overview

`edge-healthd` continuously monitors device health and writes structured status
to a JSON file. It is designed for:

- Resource-constrained edge gateways
- Offline-first operation
- Multi-platform deployment (RISC-V, ARM64)
- Integration with fleet management systems

## Supported Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| VisionFive2 | riscv64 | Primary |
| Raspberry Pi 5 | aarch64 | Supported |
| i.MX93 EVK | aarch64 | Supported |

## Features

- **Boot tracking** - Detects boot failures, tracks consecutive failures
- **Service monitoring** - Watches systemd units, detects restart loops
- **Resource monitoring** - CPU, memory, disk, temperature, network
- **Time sync status** - NTP synchronization health
- **Update awareness** - Tracks software version and update results
- **Atomic writes** - Snapshot file is always consistent
- **Systemd integration** - Notify, watchdog, journal logging

## Building

### Native Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Cross-Compile (Yocto SDK)

```bash
# Source the SDK environment
source /opt/poky/environment-setup-aarch64-poky-linux

# Build
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64.cmake \
      -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_BUILD_TESTS` | ON | Build unit tests |
| `EDGE_ENABLE_SANITIZERS` | OFF | Enable ASan/UBSan (Debug) |
| `EDGE_ENABLE_LTO` | ON | Link-time optimization (Release) |

## Installation

```bash
sudo make install
sudo systemctl enable --now edge-healthd
```

## Configuration

Configuration file: `/etc/edge/healthd.conf`

```json
{
  "device_id": "edge-001",
  "platform": "visionfive2",
  "snapshot_file": "/data/edge/health/state.json",
  "collect_interval_sec": 60,
  "monitored_services": ["sshd.service", "NetworkManager.service"],
  "monitored_mounts": ["/", "/data"],
  "thresholds": {
    "cpu_load_warn": 80,
    "mem_used_crit": 95
  }
}
```

See `config/healthd.conf.example` for full options.

## Output

Snapshot state is written to `/data/edge/health/state.json`:

```json
{
  "schema": "edge.health.state",
  "schema_version": "1.0",
  "generated_at": "2026-01-26T10:15:30Z",
  "device": { "device_id": "vf2-001", "platform": "visionfive2", ... },
  "boot": { "boot_ok": true, "uptime": 86400, ... },
  "services": { "overall": "ok", "units": [...] },
  "resources": { "cpu": {...}, "memory": {...}, ... },
  "time_sync": { "overall": "ok", "source": "ntp", ... },
  "update": { "overall": "ok", "active_slot": "A", ... },
  "summary": { "severity": "ok", "reasons": [] }
}
```

Schema: `schemas/edge.health.state.v1.0.json`
Contract: `docs/edge.health.state.v1.0.md`

## Usage

```bash
# Run as daemon (normal)
edge-healthd

# Run in foreground with verbose logging
edge-healthd -f -v

# Collect once and exit (useful for testing)
edge-healthd --once

# Use custom config
edge-healthd -c /path/to/config.json
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                       SnapshotDaemon                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐          │
│  │   Device    │  │    Boot     │  │  Services   │          │
│  │    Probe    │  │    Probe    │  │    Probe    │          │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘          │
│         │                │                │                  │
│  ┌──────┴──────┐  ┌──────┴──────┐  ┌──────┴──────┐          │
│  │  Resources  │  │  TimeSync   │  │   Update    │          │
│  │    Probe    │  │    Probe    │  │    Probe    │          │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘          │
│         │                │                │                  │
│         └────────────────┼────────────────┘                  │
│                          ▼                                   │
│                  ┌───────────────┐                           │
│                  │  Aggregator   │                           │
│                  └───────┬───────┘                           │
│                          ▼                                   │
│                  ┌───────────────┐                           │
│                  │    Writer     │ ──▶ /data/edge/health/   │
│                  └───────────────┘                           │
└─────────────────────────────────────────────────────────────┘
```

## Dependencies

- C++23 compiler (GCC 13+ or Clang 17+)
- CMake 3.20+
- nlohmann/json (fetched automatically)
- libsystemd (optional, for sd-bus/sd-journal)
- sdbus-c++ 2.1.0+ (required, for D-Bus integrations; set `EDGE_FETCH_SDBUSCPP=ON` to fetch in non-Yocto builds)

## Journal Permissions

Reading the system journal requires appropriate permissions. `edge-healthd` attempts to open the journal in this order:

- `SD_JOURNAL_SYSTEM` (system-wide journal — requires root or membership in the `systemd-journal` group)
- `SD_JOURNAL_CURRENT_USER` (per-user journal — usable by the service user when the journal is available to that user)
- `SD_JOURNAL_LOCAL_ONLY` (local-only fallback for systems without a persistent system journal)

Recommendations for production deployments:

- Prefer running `edge-healthd` as a system service and grant it journal read access by adding the `systemd-journal` group to the service's supplementary groups. Example snippet for the unit file:

```ini
[Service]
User=edge-healthd
SupplementaryGroups=systemd-journal
```

- Alternatively, add the `edge-healthd` runtime user to the `systemd-journal` group on the host:

```bash
sudo usermod -aG systemd-journal edge-healthd
```

- If the device runs without a system journal (volatile/ram journal or minimal images), the daemon will fall back to `SD_JOURNAL_LOCAL_ONLY` or the per-user journal if available.

Security note: avoid shelling out to `journalctl` from the daemon — we use `sd_journal` for safer, parsed access.


## Testing

```bash
cd build
ctest --output-on-failure
```

Schema validation (uses `uv` with an inline dependency):

```bash
uv run validate_schema.py /data/edge/health/state.json
```

## Yocto Integration

This project can be included in Yocto builds via a recipe that fetches from git:

```bitbake
SRC_URI = "git://github.com/your-org/edge-healthd.git;branch=main;protocol=https"
inherit cmake systemd
```

See your meta layer's `recipes-support/edge-healthd/` for the full recipe.

## License

MIT
