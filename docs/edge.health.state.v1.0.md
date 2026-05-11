# Edge Health Snapshot — Contract v1.0

- **Schema ID:** `edge.health.state`
- **Schema version:** `1.0`
- **Schema file:** [`schemas/edge.health.state.v1.0.json`](../schemas/edge.health.state.v1.0.json)
- **Purpose:** Offline-first device health snapshot for edge gateways (RPi5 / VisionFive2 / i.MX93)

This document describes the *contract* — semantics, field meanings, evolution rules. The schema file is the authoritative shape; this document is the authoritative interpretation.

## 1. Contract principles

- **Snapshot model.** The daemon periodically writes a *full* snapshot at `collect_interval_sec`. Consumers read the latest file; there is no delta protocol.
- **Atomic writes.** Snapshot is written to a temp file, `fsync`ed, then `rename(2)`-ed over the target. Readers never observe partial JSON.
- **Optional subsystems degrade gracefully.** When a backing service is absent (RAUC, timedated, journald) the corresponding subsystem reports `severity: "unknown"` or omits optional fields; the snapshot remains valid.
- **Additive evolution.** New optional fields may appear within `1.x` without bumping the version. Consumers MUST ignore unknown fields. Breaking changes require a new major (`2.0`).

## 2. Top-level object

The top-level snapshot is an object with the following required members:

| Field | Type | Notes |
|---|---|---|
| `schema` | `const "edge.health.state"` | |
| `schema_version` | `const "1.0"` | |
| `generated_at` | RFC 3339 UTC string | Wall-clock time the snapshot was emitted |
| `device` | object | §6.1 |
| `boot` | object | §6.2 |
| `services` | object | §6.3 |
| `resources` | object | §6.4 |
| `time_sync` | object | §6.5 |
| `update` | object | §6.6 |
| `journal` | object | §6.7 |
| `summary` | object | §6.9 |

Optional top-level members:

| Field | Type | Notes |
|---|---|---|
| `crash` | object | §6.8 — required-status deferred to v1.1 |
| `cycle` | unsigned integer | Monotonic counter, incremented every collection cycle. Consumers can detect daemon liveness without parsing log lines. Not in the v1.0 schema as a required field — present at runtime, accepted via `additionalProperties`. |

**Recommended file path:** `/run/health/state.json` (tmpfs; configurable via `snapshot_file`).

## 3. Timestamps

All timestamps are **RFC 3339 UTC** with `Z` suffix. Example: `2026-05-11T10:58:33Z`.

Time-bearing fields:

- `generated_at`
- `boot.last_boot_at`
- `services.units[].since` (nullable)
- `time_sync.ntp.last_sync_at` (nullable)
- `time_sync.ptp.last_sync_at` (nullable)
- `update.last_update.installed_at` (nullable)
- `crash.last_panic_at` (nullable)
- `crash.artifacts[].mtime` (nullable)

## 4. Severity model

Enum: `ok | warn | crit | unknown`. Aggregation follows **worst-wins**:

```
crit > warn > ok > unknown
```

Each subsystem (`services`, `time_sync`, `update`, `journal`, plus `crash` when present) reports its own `overall`. The top-level `summary.severity` is the aggregate.

## 5. Reason codes (`summary.reasons[]`)

Each entry is a **stable, machine-readable code**. Schema enforces the pattern:

```
^[a-z][a-z0-9_]*(:[a-zA-Z0-9_.:/-]+)?$
```

Rules:

- lowercase, no spaces
- recommended form: `<category>_<condition>` or `<category>_<condition>:<subject>`

Examples:

- `svc_failed:iotgw-nftables`
- `svc_restart_storm:NetworkManager`
- `disk_used_high:/data`
- `mem_used_high`
- `time_unsynced`
- `boot_loop`
- `crash_unacked`
- `journal_errors_high`

Free-text belongs in `summary.notes` or `services.units[].detail`, never in `reasons[]`.

## 6. Object semantics

### 6.1 `device`

Read once at startup and cached — values never change at runtime.

| Field | Notes |
|---|---|
| `device_id` | Stable hardware identifier. Resolution order: `/proc/device-tree/serial-number` → `/etc/machine-id` → `"unknown"`. May be overridden via config. |
| `hostname` | From `gethostname(2)` |
| `platform` | Free-form tag from config (`rpi5`, `visionfive2`, `imx93`, …) |
| `arch` | From `uname(2)` machine field |
| `os` | `{ distro, version, build_id?, kernel }` from `/etc/os-release` + kernel release |

### 6.2 `boot`

| Field | Notes |
|---|---|
| `boot_id` | Systemd boot ID if available, else `"unknown"` |
| `last_boot_at` | `generated_at - uptime` |
| `uptime` | Seconds since boot |
| `boot_ok` | `true` once a successful collection cycle has completed and `sd_notify(READY=1)` has fired |
| `boot_fail_count` | Persistent consecutive-failure counter; reset on first successful boot |
| `last_reboot_reason` | Optional; nullable |

### 6.3 `services`

```
ServicesStatus { overall, units[] }
ServiceUnit    { name, state, severity, restart_count,
                 since?, result?, detail?, log_excerpt[]? }
```

- `overall` — worst-wins across `units[]`.
- `state` — one of `active | inactive | failed | activating | deactivating | unknown`.
- `result` — pass-through of the systemd `Result` property (e.g. `success`, `core-dump`, `socket-activated` for socket-activated units).
- `restart_count` — best-effort count since boot.
- `log_excerpt` — up to 20 bounded strings (≤ 512 chars each), populated when `log_excerpt.max_lines > 0` is configured.

### 6.4 `resources`

```
ResourcesStatus { sample_window_sec, cpu, memory,
                  storage[]?, thermal[]?, network[] }
```

- **`cpu`** — `{ load1, load5, load15 }`. Raw `/proc/loadavg` values; policy may normalize `load1` as percent-of-cores for threshold evaluation.
- **`memory`** — `{ mem_total_mb, mem_used_mb, swap_used_mb }`.
- **`storage[]`** — one entry per `monitored_mounts`. Required keys: `mount, used_pct, avail_mb`. `fs` optional.
- **`thermal[]`** — one entry per discovered `/sys/class/thermal/thermal_zone*` sensor when `enable_thermal: true`.
- **`network[]`** — one entry per interface. When `monitored_interfaces` is empty (default), **all non-loopback interfaces are reported automatically**. Counters (`rx_bytes`, `tx_bytes`, `rx_packets`, `tx_packets`, `rx_dropped`, `tx_dropped`, `rx_err`, `tx_err`) come from Netlink (`RTM_GETLINK`) — no `/sys/class/net` polling. Optional: `ip` (IPv4), `carrier`, `speed_mbps`, `duplex`.

### 6.5 `time_sync`

```
TimeSyncStatus { overall, source, ntp, ptp, rtc }
```

- `source` — `none | ntp | ptp`. Reflects which time source is currently authoritative.
- **`ntp`** — `{ enabled, state?, last_sync_at? }`. `state ∈ locked | free_running | holdover | unknown`. Populated from `org.freedesktop.timedate1`.
- **`ptp`** — `{ enabled, interface?, offset_ns?, rms_ns?, state?, last_sync_at?, role? }`. No active PTP probe is implemented yet in v1.0; the field reflects configuration only. Populating it is reserved for a future probe.
- **`rtc`** — `{ enabled, hctosys?, voltage_mv?, drift_sec? }`. When `enable_rtc: true` and `rtc_device` is readable, the probe reports:
  - `hctosys` — whether the kernel copied RTC → system clock at boot (from `/sys/class/rtc/<rtc>/hctosys`).
  - `voltage_mv` — backup battery voltage in millivolts (from `voltage`/`voltage_now` sysfs node, μV converted).
  - `drift_sec` — signed seconds between RTC and system clock at probe time.
  - Severity thresholds: `rtc_voltage_warn_mv` (default 2700) and `rtc_voltage_crit_mv` (default 2500).

Polling is decoupled from `collect_interval_sec` via `time_sync_interval_sec` (default 300 s) to avoid socket-activating `systemd-timedated` on every cycle.

> The `rtc` member is present at runtime but is not yet listed in the v1.0 schema's `time_sync.properties`; it is accepted via `additionalProperties: true`. It will be formalized in the schema in a future additive update.

### 6.6 `update`

```
UpdateStatus { overall, active_slot?, last_update? }
LastUpdate   { id, result, installed_at?, detail? }
```

- `active_slot` — RAUC-style A/B slot name (e.g. `"A"`, `"B"`), nullable.
- `result` — `success | failed | unknown`.
- `detail` — free-form (e.g. bundle compatible string).

Polling is decoupled via `update_check_interval_sec` (default 1800 s). The daemon also subscribes to `RAUC.Installer.Completed` and forces an immediate refresh on signal — so a freshly installed bundle is reflected in the next snapshot without waiting for the poll interval.

When RAUC is not present on the platform, `overall` is `unknown` and `last_update` is omitted.

### 6.7 `journal`

```
JournalStatus { overall, error_count, recent_errors[] }
```

- System-wide journal scan covering entries with priority ≤ 3 (`err`/`crit`/`alert`/`emerg`) within `log_excerpt.window_sec`.
- `error_count` — number of matching entries in the window.
- `recent_errors[]` — newest first, capped at `log_excerpt.max_lines`, each ≤ 512 chars.
- Severity: `ok` when `error_count == 0`; `warn`/`crit` thresholds are policy-driven.
- Scan budget bounded by `log_excerpt.scan_timeout_ms` (default 3000) to protect cycle latency.

The D-Bus method `edge.health.Manager.GetRecentLogs` serves the *same* cached buffer with zero `sd_journal_open()` overhead.

### 6.8 `crash`

```
CrashStatus { present, artifact_count, artifacts[], acknowledged,
              source?, last_panic_at?, fingerprint? }
CrashArtifact { name, size_bytes, mtime? }
```

- Sourced from `/var/lib/systemd/pstore` (kernel-panic artifacts captured by `systemd-pstore`).
- `source` — `"pstore"` when present.
- `fingerprint` — FNV-1a hash over `(name, size_bytes, mtime)` across all artifacts. Stable per panic event; changes only when pstore contents change.
- `acknowledged` — `true` when an external actor (operator tool or future `AcknowledgeCrash()` D-Bus method) has confirmed the fingerprint. The probe itself is **read-only**: it never writes the acknowledgement state.
- Daemon emits `HealthAlarm(component="crash", message=…, severity="crit")` once per new unacknowledged fingerprint and re-arms when pstore is cleared.

> `crash` is defined in the schema but intentionally omitted from the top-level `required[]` to remain compatible with v1.0 validators predating PR #35. Promotion to required is planned for v1.1.

### 6.9 `summary`

```
SnapshotSummary { severity, reasons[], notes? }
```

- `severity` — worst-wins aggregate (§4).
- `reasons[]` — list of stable codes (§5).
- `notes` — optional free-form human commentary.

## 7. Sibling D-Bus contract

The daemon exposes a system-bus service (`edge.health` / `/edge/health/manager` / `edge.health.Manager`) that mirrors snapshot state for push consumers:

| Member | Kind | Purpose |
|---|---|---|
| `OverallSeverity` | property `s` | Tracks `summary.severity` |
| `TriggerSnapshot` | method `() → b` | Force an immediate cycle; returns `false` when rate-limited |
| `GetRecentLogs` | method `(u) → as` | Cached `journal.recent_errors` |
| `HealthAlarm` | signal `(s, s, s)` | `(component, message, severity)` on degradation or new crash |

D-Bus is not required for snapshot consumption — file readers see the same data.

## 8. Compatibility & evolution

- **v1.x rule:** changes must be **additive** — new optional fields, new reason codes, new severity-input subsystems. Consumers MUST ignore unknown fields.
- **No removals or renames** without a major bump.
- **No re-typing** of an existing field without a major bump.
- **Breaking changes** (removing a field, narrowing an enum, changing a required-status) require v2.0.

Planned for **v1.1** (additive, no breakage):

- Formalize `time_sync.rtc` in the schema properties.
- Formalize `cycle` at the top level.
- Promote `crash` into top-level `required[]`.
- Add `collected_at` staleness timestamps to `TimeSyncStatus` and `UpdateStatus`.

## 9. Validation

```bash
python3 scripts/validate_schema.py /run/health/state.json
```

The validator uses `jsonschema` with Draft 2020-12 and defaults to `schemas/edge.health.state.v1.0.json`.
