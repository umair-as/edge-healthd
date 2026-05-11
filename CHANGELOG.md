# Changelog

All notable changes to `edge-healthd` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Snapshot schema versioning is independent — see [`docs/edge.health.state.v1.0.md`](docs/edge.health.state.v1.0.md) §8.

## [0.6.0] — 2026-05-11

### Added
- **CrashProbe** — scans `/var/lib/systemd/pstore` for kernel-panic artifacts, FNV-1a fingerprints the artifact set, and emits a distinguishable `HealthAlarm(component="crash", severity="crit")` on each new unacknowledged fingerprint. Re-arms when pstore is cleared. (#35)
- **RAUC `Installer.Completed` subscription** — daemon forces an immediate `UpdateProbe` refresh on signal so a freshly installed bundle is reflected in the next snapshot without waiting for the poll interval. Graceful degradation when RAUC is absent. (#32)
- **Web UI sync with current daemon** — `RtcStatus`, `JournalStatus`, and `cycle` in the TypeScript types; new Journal view; RTC card under Time Sync; cycle counter and "Refresh Now" button in the Dashboard; `POST /api/trigger` endpoint via godbus. (#34)
- **TriggerSnapshot rate-limit** — `trigger_min_interval_sec` (default 5 s); throttled calls now return `false` instead of being silently coalesced. (#31)
- **`atomic_write_file()` shared primitive** — `open → write → fsync(fd) → close → rename → fsync(parent dirfd)`, with the parent-dir fsync that most projects miss. Used by both `SnapshotWriter::write_atomic` and `BootProbe::save_boot_state`. (#37, #40)
- **`scripts/benchmark.sh`** — performance profiler (strace, perf, /proc snapshots). Source of the numbers in `docs/performance-profile.md`.
- **`scripts/test-target.sh`** — 15-section on-hardware functional test suite (snapshot validity, every probe, D-Bus interface, stress-ng escalation, fio disk pressure).
- **`-allowed-origins` CLI flag on the web server** — comma-separated `host[:port]` or full-URL allowlist for the WebSocket and mutating endpoints, beyond strict same-origin.

### Changed
- **`SnapshotWriter` and `BootProbe::save_boot_state`** moved off `std::ofstream + std::filesystem::rename` onto `atomic_write_file()`. `BootProbe::save_boot_state` now surfaces write failures via the rate-limited `log::probe_error("boot", …)` path instead of silently dropping them. (#37, #40)
- **D-Bus connection sharing in the C++ daemon** — single long-lived connection passed into `ServicesProbe` / `TimeSyncProbe` / `UpdateProbe`; RAUC proxy moved onto the shared connection. Zero ephemeral D-Bus connections per cycle (was 1–2). (#33)
- **Web server `POST /api/trigger`** now reuses a single long-lived `*dbus.Conn` on the `Server` struct with lazy reconnect, mirroring the C++-side change. (#41)
- **Probe scheduling moved into the daemon** — `ProbeSchedule` plus `last_known_good_` caching in `SnapshotDaemon`. `DeviceProbe` runs once at startup and is cached; `TimeSyncProbe` and `UpdateProbe` get independent intervals (`time_sync_interval_sec`, `update_check_interval_sec`). (#26)
- **Version derivation** — `cmake/GetGitVersion.cmake` resolves version from `git describe`, with `VERSION` file as the Yocto/tarball-build fallback. (#30)
- **Config example** — adds annotated comments for every option; `//` line comments are now accepted in the JSON config. (#29)
- **README** rewritten for public consumption: TOC, full 8-probe diagram, D-Bus interface section, web UI section, performance numbers, contributing guide. Snapshot example now reflects the live schema (`cycle`, `journal`, `crash`). (#36)
- **`docs/edge.health.state.v1.0.md`** rewritten to match the live schema and code — adds `cycle`, `journal`, `crash`, `rtc` in `time_sync`, network auto-discovery, the D-Bus sibling contract, and a v1.1 evolution plan. (#36)

### Fixed
- **Snapshot atomic-write durability** — previous implementation flushed userspace buffers but never called `fsync`. A power cut between rename returning and writeback completing could leave a zero-length `state.json` on disk. (#37 via #44)
- **Boot-fail counter durability** — `BootProbe::save_boot_state` had the same `fsync` gap; a power cut could wipe the consecutive-failure ratchet and mask a boot loop near `boot_fail_crit`. (#40 via #44)
- **Web UI same-origin + CSRF** — WebSocket `CheckOrigin` previously accepted all origins; `POST /api/trigger` accepted any same-LAN request. Now WebSocket enforces same-origin (with `-allowed-origins` allowlist for dev proxies); `/api/trigger` additionally requires the custom header `X-Edge-Health: 1`, which a cross-origin browser request cannot set without a CORS preflight that the server intentionally fails. (#39 via #42)
- **`enable_ptp` runtime/config contract** — flag now surfaces in the snapshot and a clearer startup warning is emitted when PTP is requested without an active probe implementation. (#25)
- **`STATUS` line on systemd notify** — `notify_ready` no longer overwrites the live severity line after the first cycle. (#23)

### Removed
- **Stale draft schemas** — `schemas/edge.health.state.v2.0.json` (older than v1.0, missing `journal`/`crash`, never referenced) and the five unused `schemas/edge.health.network.*.schema.json` drafts including the `netfliter` typo. (#36)
- **Personal/legacy references** — sanitized hardcoded `/home/umair/yocto_resource/...` paths in `CLAUDE.md`; fixed stale `github.com/umair-uas/...` org references in `systemd/edge-healthd.service` and `web/server/go.mod`. (#36)

### Security
- Web UI same-origin and CSRF guards close a LAN-resident cross-site abuse vector against the opt-in `EDGE_WEB_UI=ON` component. Not exploitable on the production iot-gateway image (web UI is not built in). (#39 via #42)

## [0.5.1] — 2026-04-09

### Added
- **RTC probe** — battery voltage (sysfs µV → mV), `hctosys` flag, signed `drift_sec` against system clock. Surfaced inside `TimeSyncStatus.rtc`. Thresholds: `rtc_voltage_warn_mv` (default 2700), `rtc_voltage_crit_mv` (default 2500). (#24)
- **Monotonic cycle counter** — `"cycle": N` in the top-level snapshot, increments every `collection_cycle()`. Consumers can detect daemon liveness without parsing the rate-limited "Snapshot collected" log line. (#28)

### Changed
- **README** synced with current implementation: timedated backend, RTC probe, decoupled polling cadences, cycle counter note. (#27)

## [0.5.0] — 2026-04-07

### Added
- Initial tagged release after extensive pre-tag development. See `git log v0.5.0` for the full series.

Notable pre-0.5.0 features that are part of this release:
- **Snapshot daemon** with `sd-notify` ready + watchdog heartbeat, signal handling, atomic snapshot writes.
- **Probes** — Device, Boot, Services, Resources, TimeSync, Update.
- **Netlink monitor** — zero-poll interface stats via libmnl (`RTM_GETLINK`).
- **Standalone `JournalProbe`** — system-wide error scanning (priority ≤ 3), feeds overall severity. (#13)
- **`edge.health.Manager` D-Bus service** — `OverallSeverity` property, `TriggerSnapshot` method, `HealthAlarm` signal. (#18)
- **`GetRecentLogs` D-Bus method** — in-memory cache, zero `sd_journal_open()` overhead. (#20)
- **`JournalProbe::collect()` deadline enforcement.** (#19)
- **NTP polling decoupled from `collect_interval`** via `time_sync_interval_sec`. (#21)
- **Optional web UI** — Go server with HTTPS, Preact + TypeScript dashboard, WebSocket push, dark mode. Built behind `EDGE_WEB_UI=ON`. (#11)

[0.6.0]: https://github.com/umair-as/edge-healthd/releases/tag/v0.6.0
[0.5.1]: https://github.com/umair-as/edge-healthd/releases/tag/v0.5.1
[0.5.0]: https://github.com/umair-as/edge-healthd/releases/tag/v0.5.0
