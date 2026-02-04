# Edge Observability Contract v1.0

Schema ID: edge.health.state
Schema Version: 1.0
Purpose: Offline-first device health snapshot for edge gateways (RPi5 / VisionFive2 / i.MX93)

## 1. Contract principles

- Snapshot model: daemon periodically writes a full snapshot; consumers read the latest.
- Atomic writes: write to temp + rename; never partial JSON.
- Optional subsystems: fields for optional subsystems (PTP, RAUC, journald access, etc.) are optional; absence means unknown or not available.

## 2. Timestamps

All timestamps are UTC RFC3339 with Z.

Example: 2026-01-25T10:58:33Z

Time fields:

- generated_at
- boot.last_boot_at
- services.units[].since (optional)
- time_sync.ntp.last_sync_at (optional)
- time_sync.ptp.last_sync_at (optional)
- update.last_update.installed_at (optional)

## 3. Severity model

Enum: ok | warn | crit | unknown

Aggregation uses worst wins:

crit > warn > ok > unknown

## 4. Reason codes (summary.reasons[])

Each entry is a stable machine-readable reason code.

Rules:

- lowercase
- no spaces
- recommended patterns:
  - <category>_<condition>
  - <category>_<condition>:<subject>

Examples:

- svc_failed:iotgw-nftables
- svc_restart_storm:NetworkManager
- disk_used_high:/data
- mem_used_high
- ptp_offset_high:eth0
- time_unsynced

No free-text in reasons[]. Free-text goes to summary.notes or services.units[].detail.

## 5. Top-level snapshot object

Recommended file path: /data/edge/health/state.json (configurable; keep stable per product)

Required top-level fields:

- schema = edge.health.state
- schema_version = 1.0
- generated_at (UTC RFC3339)
- device, boot, services, resources, time_sync, update, summary

## 6. Object semantics

### 6.1 device

- device_id: configured or derived from /etc/machine-id (never empty; use "unknown" if needed)
- hostname: from gethostname or /etc/hostname
- platform: configured (rpi5, visionfive2, imx93, etc.)
- arch: from uname
- os: from /etc/os-release plus kernel version

### 6.2 boot

- boot_id: systemd boot ID if available else "unknown"
- uptime: seconds since boot
- last_boot_at: generated_at - uptime
- boot_fail_count: consecutive failures counter (persistent)
- boot_ok: indicates current boot deemed successful (set after first successful collection + READY)
- last_reboot_reason: optional

### 6.3 services

ServicesStatus

- overall: worst unit severity (policy may incorporate criticality later)
- units[]: list of ServiceUnit

ServiceUnit

- name: e.g. iotgw-nftables.service
- state: active | inactive | failed | activating | deactivating | unknown
- severity: derived health severity
- since: optional timestamp when state began
- restart_count: restarts since boot (best-effort)
- result: optional, pass-through systemd Result string
- detail: optional bounded human detail

### 6.4 resources

- sample_window_sec: sampling window for counters
- cpu.load1/load5/load15: raw loadavg values (not percent)
  - policy: thresholds may normalize load1 as percent cores for evaluation
- memory: total, used, swap used (MB)
- storage[]: per monitored mount (used_pct, avail_mb, fs)
- thermal[]: optional sensors list
- network[]: per monitored interface
  - link: up | down | unknown (carrier where possible)
  - ip: optional IPv4 address
  - counters: rx_bytes, tx_bytes, rx_packets, tx_packets, rx_dropped, tx_dropped, rx_err, tx_err
  - optional: carrier, speed_mbps, duplex

### 6.5 time_sync (PTP included)

- overall: severity for time sync
- source: none | ntp | ptp

ntp

- enabled
- state: locked | free_running | holdover | unknown (optional)
- last_sync_at: optional

ptp (optional object, present if enabled or supported)

- enabled
- interface: optional string
- offset_ns: optional int
- rms_ns: optional uint
- state: optional time sync state
- last_sync_at: optional UTC RFC3339
- role: optional string (master | slave | unknown)

### 6.6 update

- overall: ok | warn | unknown
- active_slot: optional (RAUC-style)
- last_update: optional
  - id
  - installed_at: optional UTC RFC3339
  - result: success | failed | unknown
  - detail: optional

### 6.7 summary

- severity: overall snapshot severity
- reasons[]: stable reason codes
- notes: optional human notes

## 7. Compatibility

- v1.x changes must be additive (new optional fields or new reason codes).
- breaking changes require v2.0.
