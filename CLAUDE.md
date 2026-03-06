# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

edge-healthd is a lightweight health monitoring daemon for resource-constrained edge gateways (RISC-V, ARM64). It collects device metrics via probes and writes atomic JSON snapshots to disk. Targets: VisionFive2 (RV64), Raspberry Pi 5, i.MX93 EVK.

## Build Commands

```bash
# Configure (Debug with tests and sanitizers)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DEDGE_BUILD_TESTS=ON -DEDGE_ENABLE_SANITIZERS=ON \
  -DEDGE_FETCH_SDBUSCPP=ON

# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build --output-on-failure -R test_name

# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDGE_FETCH_SDBUSCPP=ON
cmake --build build

# Validate snapshot against schema
python3 scripts/validate_schema.py /data/edge/health/state.json
```

**System dependencies:** cmake (3.20+), ninja-build, GCC 13+ or Clang 17+ (C++23), libsystemd-dev, libmnl-dev, pkg-config. sdbus-c++ v2.0 can be auto-fetched with `EDGE_FETCH_SDBUSCPP=ON`.

## Yocto SDK Cross-Compilation (Raspberry Pi 5)

The project is cross-compiled for the IoT gateway using a Yocto-generated SDK:

- **SDK path:** `/home/umair/yocto_resource/rpi5-sdk`
- **Distro:** `iotgw igw.0.1`
- **Target:** `cortexa76-oe-linux` (ARM64 / Cortex-A76, Raspberry Pi 5)
- **Toolchain cmake file:** `sysroots/x86_64-oesdk-linux/usr/share/cmake/cortexa76-oe-linux-toolchain.cmake`

```bash
# Source the SDK environment (sets CC, CXX, SYSROOT, PKG_CONFIG_PATH, etc.)
source /home/umair/yocto_resource/rpi5-sdk/environment-setup-cortexa76-oe-linux

# Cross-compile release build (use a separate build dir to avoid clobbering native build)
cmake -B build-rpi5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/home/umair/yocto_resource/rpi5-sdk/sysroots/x86_64-oesdk-linux/usr/share/cmake/cortexa76-oe-linux-toolchain.cmake \
  -DEDGE_FETCH_SDBUSCPP=ON

cmake --build build-rpi5
```

**Cross-compilation note for code generation tools:** When `EDGE_FETCH_SDBUSCPP=ON`, `sdbus-c++-xml2cpp` is compiled for the *target* (ARM64) and cannot run on the host (x86_64). Any generated files (e.g. D-Bus adaptor headers) must be pre-generated on a native build and committed to the repo. The CMake `edge-dbus-generate` custom target is only available in native builds.

## Architecture

**Probe → Aggregator → Writer pipeline:**

- **Probes** (`inc/probes.hpp`, `src/probes.cpp`, `src/probes_dbus.cpp`): Each probe implements the `Probe` concept — a `DataType` alias and a `collect()` method returning `ProbeResult<DataType>` (alias for `std::expected`). Six probes: DeviceProbe, BootProbe, ServicesProbe, ResourcesProbe, TimeSyncProbe, UpdateProbe.
- **SnapshotAggregator** (`inc/aggregator.hpp`, `src/aggregator.cpp`): Runs all probes, evaluates severity thresholds (ok/warn/crit) per subsystem, produces a `SnapshotState`.
- **SnapshotWriter** (`inc/writer.hpp`, `src/writer.cpp`): Atomic writes via temp file + fsync + rename.
- **SnapshotDaemon** (`inc/daemon.hpp`, `src/daemon.cpp`): Main loop with systemd notify/watchdog integration, signal handling (SIGTERM/SIGINT).
- **NetlinkMonitor** (`inc/netlink_monitor.hpp`, `src/netlink_monitor.cpp`): Zero-syscall network stats via libmnl (RTM_GETLINK).

**Key source layout:**
- `inc/` — public headers (types, config, probes, aggregator, writer, daemon, log, json)
- `src/` — implementation (~3700 LOC)
- `tests/` — Catch2 unit tests (~670 LOC)
- `config/` — example config files
- `schemas/` — JSON schema definitions
- `web/` — optional Preact+Go web dashboard (built with `EDGE_WEB_UI=ON`)

## Conventions

- **C++23** with `std::expected` for error handling — no exceptions
- All enums have `constexpr to_string()` conversions in `inc/types.hpp`
- JSON serialization uses nlohmann/json with `to_json`/`from_json` ADL in `src/json.cpp`
- Config is JSON-based, loaded from `/etc/edge/healthd.conf` (see `inc/config.hpp`, `src/config.cpp`)
- Logging via `inc/log.hpp`: thread-safe, rate-limited, journal-aware; uses named functions like `log::probe_error()`, `log::snapshot_collected()`
- Strict compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow` + several `-Werror` flags)
- Compile definitions: `EDGE_HAS_SYSTEMD=1` and `EDGE_HAS_LIBMNL=1` when respective libraries are found

## CMake Build Options

| Option | Default | Description |
|---|---|---|
| `EDGE_BUILD_TESTS` | OFF | Build Catch2 unit tests |
| `EDGE_ENABLE_SANITIZERS` | OFF | ASan + UBSan for Debug builds |
| `EDGE_ENABLE_LTO` | ON | Link-time optimization for Release |
| `EDGE_FETCH_SDBUSCPP` | OFF | Auto-fetch sdbus-c++ v2.0 |
| `EDGE_WEB_UI` | OFF | Build optional Go web dashboard |

## Git Workflow

- **Never push directly to `main`** — always use a feature branch + PR
- **Conventional Commits**: `type(scope): description` (e.g., `fix(netlink):`, `feat(daemon):`, `refactor(probes):`)
  - Types: `feat`, `fix`, `refactor`, `build`, `docs`, `test`, `chore`, `perf`
- **Squash merge** PRs onto `main` to keep history clean (one commit per feature/fix)
- Branch naming: `feat/short-description`, `fix/short-description`, `refactor/short-description`
