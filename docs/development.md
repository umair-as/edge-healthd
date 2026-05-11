# Development Guide

This document covers building `edge-healthd` from source, cross-compiling against a Yocto SDK, the CMake option surface, testing, and benchmarking. For day-to-day operation, see [`usage.md`](usage.md). For contributor workflow, see [Contributing](#contributing) below.

## Contents

- [Requirements](#requirements)
- [Native build](#native-build)
- [Cross-compile (Yocto SDK)](#cross-compile-yocto-sdk)
- [CMake options](#cmake-options)
- [Testing](#testing)
- [Benchmarking](#benchmarking)
- [Code layout](#code-layout)
- [Contributing](#contributing)

## Requirements

- **CMake** ≥ 3.20
- **Ninja**
- **GCC** ≥ 13 (or **Clang** ≥ 17) — C++23 is required
- **libsystemd-dev**
- **libmnl-dev**
- **pkg-config**

`sdbus-c++` ≥ v2.0 can be auto-fetched at configure time with `-DEDGE_FETCH_SDBUSCPP=ON` (default OFF; pick this on systems where the distro doesn't package v2.0 yet).

For the optional web UI:

- **Go** ≥ 1.22
- **Node.js** ≥ 20 + `npm` (for the Preact frontend)

## Native build

```bash
# Debug — full tests, sanitizers, FetchContent sdbus-c++
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DEDGE_BUILD_TESTS=ON -DEDGE_ENABLE_SANITIZERS=ON \
  -DEDGE_FETCH_SDBUSCPP=ON
cmake --build build

# Run the test suite
ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build --output-on-failure -R atomic_file

# Release
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDGE_FETCH_SDBUSCPP=ON
cmake --build build
```

### Useful environment for development

```bash
# Foreground with verbose logging — bypasses systemd
./build/edge-healthd -f -v -c config/local.conf.example

# One-shot snapshot (no daemon loop)
./build/edge-healthd -c config/local.conf.example --once

# Schema-validate the produced snapshot
python3 scripts/validate_schema.py /tmp/edge-healthd/state.json
```

## Cross-compile (Yocto SDK)

Source the SDK environment first (it sets `CC`, `CXX`, `SYSROOT`, `PKG_CONFIG_PATH`, and `OECORE_NATIVE_SYSROOT`):

```bash
export SDK_PATH=/path/to/rpi5-sdk
source "$SDK_PATH/environment-setup-cortexa76-oe-linux"   # RPi5 / i.MX93
# source "$SDK_PATH/environment-setup-riscv64-oe-linux"   # VisionFive2

cmake -B build-target -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DEDGE_FETCH_SDBUSCPP=ON \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake"
cmake --build build-target
```

### Pre-generated D-Bus adaptor (cross-build constraint)

When `EDGE_FETCH_SDBUSCPP=ON`, `sdbus-c++-xml2cpp` is compiled for the **target** architecture, so the host CMake cannot execute it during a cross-build. The adaptor header is therefore pre-generated and committed at [`inc/generated/HealthManagerAdaptor.hpp`](../inc/generated/HealthManagerAdaptor.hpp).

If you modify [`dbus/edge-health-manager.xml`](../dbus/edge-health-manager.xml), regenerate the header via a **native** build:

```bash
cmake --build build --target edge-dbus-generate
```

Then commit the regenerated `inc/generated/HealthManagerAdaptor.hpp` alongside the XML change. The CMake `edge-dbus-generate` target is only available in native builds.

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `EDGE_BUILD_TESTS` | OFF | Build Catch2 unit tests |
| `EDGE_ENABLE_SANITIZERS` | OFF | ASan + UBSan (Debug builds only) |
| `EDGE_ENABLE_LTO` | ON | Link-time optimization (Release builds) |
| `EDGE_FETCH_SDBUSCPP` | OFF | Auto-fetch sdbus-c++ v2.0 via FetchContent |
| `EDGE_WEB_UI` | OFF | Build optional Go + Preact web dashboard |

The build also defines `EDGE_HAS_SYSTEMD=1` and `EDGE_HAS_LIBMNL=1` automatically when the respective libraries are found. Strict warning flags (`-Wall -Wextra -Wpedantic -Wshadow` + several `-Werror=...` flags) are always on.

## Testing

### Unit tests (host)

```bash
ctest --test-dir build --output-on-failure
```

Catch2-based. Coverage targets the data structures, JSON serialization, severity aggregation, atomic-write helper, journal scanner, and writer. Sanitizer-clean (ASan + UBSan) under Debug is a hard CI requirement.

### On-target functional tests

[`scripts/test-target.sh`](../scripts/test-target.sh) is a 15-section harness covering service health, snapshot validity, schema, cycle counter, every probe, D-Bus interface, ProbeSchedule cadence, journal, plus `stress-ng` and `fio` for severity escalation + recovery.

```bash
# From the host; set REMOTE to your gateway's ssh alias or user@host
export REMOTE=gateway
ssh $REMOTE 'bash -s' < scripts/test-target.sh
ssh $REMOTE 'bash -s' < scripts/test-target.sh 2>&1 | tee test-$(date +%Y%m%d).log
```

Exit code is 0 on all-pass / 1 on any failure.

### Schema validation

```bash
python3 scripts/validate_schema.py /run/health/state.json
```

Uses `jsonschema` (Draft 2020-12) against `schemas/edge.health.state.v1.0.json`.

## Benchmarking

[`scripts/benchmark.sh`](../scripts/benchmark.sh) profiles a running daemon using `strace -c`, timestamped `strace -T -tt`, `perf stat`, and `/proc` snapshots. Output lands in `/data/edge-healthd-benchmark/<utc-timestamp>/` on the target.

```bash
ssh $REMOTE 'bash -s' < scripts/benchmark.sh
```

Reference numbers and methodology live in [`performance-profile.md`](performance-profile.md).

## Code layout

```
inc/                  Public headers (types, config, probes, aggregator,
                      writer, daemon, log, json, dbus_manager, atomic_file)
inc/generated/        Pre-generated D-Bus adaptor header (committed)
src/                  Implementation
tests/                Catch2 unit tests
config/               Annotated example configs + D-Bus policy
dbus/                 D-Bus interface XML
schemas/              JSON schema definitions
scripts/              Operator scripts (validate, benchmark, test-target)
systemd/              Service unit
web/                  Optional Go server + Preact UI
docs/                 This guide, usage guide, schema contract, perf profile
```

Each probe implements the same shape — a `DataType` alias and a `collect()` method returning `std::expected<DataType, ProbeError>`. The `SnapshotAggregator` runs them on independent schedules (master cadence + decoupled NTP/RAUC intervals), evaluates per-subsystem severity thresholds, and produces a `SnapshotState`. The writer commits it atomically via the shared `atomic_write_file()` primitive (open → write → fsync(fd) → close → rename → fsync(parent dirfd)).

## Contributing

Issues and pull requests are welcome.

### Workflow

- **Code, behavior, schemas, configs, CMake, CI workflows** → feature branch + PR + CI. Non-negotiable.
- **Docs-only changes** (this file, the README, `docs/*`) → may land directly on `main` for small edits.
- Branch naming: `feat/short-description`, `fix/short-description`, `refactor/short-description`, `docs/short-description`.

### Commits

- **Conventional Commits**: `type(scope): description`
- Types: `feat`, `fix`, `refactor`, `build`, `docs`, `test`, `chore`, `perf`
- One commit per feature/fix; PRs are squash-merged onto `main`.

### Before submitting

- `ctest --test-dir build --output-on-failure` passes
- ASan + UBSan clean under Debug
- No new compiler warnings
- For larger changes, open an issue first to discuss scope

### Releases

Versioning is SemVer; the CHANGELOG (`CHANGELOG.md`) is updated as part of any release-grade change. Version derivation is via `git describe` (see `cmake/GetGitVersion.cmake`) with the `VERSION` file as the Yocto/tarball-build fallback. Tags are signed annotated tags of the form `vMAJOR.MINOR.PATCH`.
