# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Canonical Guidance

`AGENTS.md` is the canonical source for repository workflow and guardrails:

- build/test commands
- coding conventions and compatibility rules
- D-Bus codegen policy
- documentation update policy
- commit/PR/security hygiene

When `CLAUDE.md` and `AGENTS.md` differ, follow `AGENTS.md` and treat this file as stale until updated.

## Project Overview (Claude Quick Context)

edge-healthd is a lightweight health monitoring daemon for resource-constrained edge gateways (RISC-V, ARM64). It collects device metrics via probes and writes atomic JSON snapshots to disk. Targets: VisionFive2 (RV64), Raspberry Pi 5, i.MX93 EVK.

## Claude-Specific Notes

- Prefer referencing `AGENTS.md` instead of copying command blocks into responses or commits.
- If you add new repo rules, update `AGENTS.md` first, then keep `CLAUDE.md` as a thin pointer.
- Use `/run/health/state.json` for snapshot validation examples (not `/data/...`).

## Yocto SDK Cross-Compilation (Raspberry Pi 5)

Cross-compiled using a Yocto-generated SDK. Set `SDK_PATH` to wherever your SDK is installed:

- **Target:** `cortexa76-oe-linux` (ARM64 / Cortex-A76, Raspberry Pi 5)
- **Toolchain cmake file:** `$SDK_PATH/sysroots/x86_64-oesdk-linux/usr/share/cmake/cortexa76-oe-linux-toolchain.cmake`

```bash
export SDK_PATH=/path/to/rpi5-sdk

# Source the SDK environment (sets CC, CXX, SYSROOT, PKG_CONFIG_PATH, etc.)
source "$SDK_PATH/environment-setup-cortexa76-oe-linux"

# Cross-compile release build (use a separate build dir to avoid clobbering native build)
cmake -B build-rpi5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$SDK_PATH/sysroots/x86_64-oesdk-linux/usr/share/cmake/cortexa76-oe-linux-toolchain.cmake" \
  -DEDGE_FETCH_SDBUSCPP=ON

cmake --build build-rpi5
```

**Cross-compilation note for code generation tools:** When `EDGE_FETCH_SDBUSCPP=ON`, `sdbus-c++-xml2cpp` is compiled for the *target* (ARM64) and cannot run on the host (x86_64). Any generated files (e.g. D-Bus adaptor headers) must be pre-generated on a native build and committed to the repo. The CMake `edge-dbus-generate` custom target is only available in native builds.
