// SPDX-License-Identifier: MIT
// edge-healthd: Core type definitions
// These types map directly to the edge.health.state JSON schema (v1.0)

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace edge {

// -----------------------------------------------------------------------------
// Enumerations (match JSON schema enums exactly)
// -----------------------------------------------------------------------------

enum class Severity { Ok, Warn, Crit, Unknown };

enum class ServiceState {
    Active,
    Inactive,
    Failed,
    Activating,
    Deactivating,
    Unknown
};

enum class LinkState { Up, Down, Unknown };

enum class TimeSyncSource { None, Ntp, Ptp };

enum class TimeSyncState { Locked, FreeRunning, Holdover, Unknown };

enum class UpdateResult { Success, Failed, Unknown };

// -----------------------------------------------------------------------------
// String conversion (for JSON serialization)
// -----------------------------------------------------------------------------

[[nodiscard]] constexpr std::string_view to_string(Severity s) noexcept {
    switch (s) {
        case Severity::Ok:      return "ok";
        case Severity::Warn:    return "warn";
        case Severity::Crit:    return "crit";
        case Severity::Unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ServiceState s) noexcept {
    switch (s) {
        case ServiceState::Active:       return "active";
        case ServiceState::Inactive:     return "inactive";
        case ServiceState::Failed:       return "failed";
        case ServiceState::Activating:   return "activating";
        case ServiceState::Deactivating: return "deactivating";
        case ServiceState::Unknown:      return "unknown";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(LinkState s) noexcept {
    switch (s) {
        case LinkState::Up:      return "up";
        case LinkState::Down:    return "down";
        case LinkState::Unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(TimeSyncSource s) noexcept {
    switch (s) {
        case TimeSyncSource::None: return "none";
        case TimeSyncSource::Ntp:  return "ntp";
        case TimeSyncSource::Ptp:  return "ptp";
    }
    return "none";
}

[[nodiscard]] constexpr std::string_view to_string(TimeSyncState s) noexcept {
    switch (s) {
        case TimeSyncState::Locked:     return "locked";
        case TimeSyncState::FreeRunning: return "free_running";
        case TimeSyncState::Holdover:   return "holdover";
        case TimeSyncState::Unknown:    return "unknown";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(UpdateResult r) noexcept {
    switch (r) {
        case UpdateResult::Success: return "success";
        case UpdateResult::Failed:  return "failed";
        case UpdateResult::Unknown: return "unknown";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// Data structures (leaf types first, composites later)
// -----------------------------------------------------------------------------

struct OsInfo {
    std::string distro;
    std::string version;
    std::string build_id;
    std::string kernel;
};

struct DeviceInfo {
    std::string device_id;
    std::string hostname;
    std::string platform;      // e.g., "visionfive2", "rpi5", "imx93"
    std::string arch;          // e.g., "riscv64", "aarch64"
    OsInfo os;
};

struct BootStatus {
    std::string boot_id;       // systemd boot ID (UUID)
    std::chrono::system_clock::time_point last_boot_at;
    std::chrono::seconds uptime;
    bool boot_ok = true;
    uint32_t boot_fail_count = 0;
    std::optional<std::string> last_reboot_reason;
};

struct ServiceUnit {
    std::string name;
    ServiceState state = ServiceState::Unknown;
    Severity severity = Severity::Unknown;
    std::optional<std::chrono::system_clock::time_point> since;
    uint32_t restart_count = 0;
    std::optional<std::string> result;
    std::optional<std::string> detail;
};

struct ServicesStatus {
    Severity overall = Severity::Unknown;
    std::vector<ServiceUnit> units;
};

struct CpuLoad {
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
};

struct MemoryUsage {
    uint64_t mem_total_mb = 0;
    uint64_t mem_used_mb = 0;
    uint64_t swap_used_mb = 0;
};

struct StorageMount {
    std::string mount;
    std::string fs;
    uint8_t used_pct = 0;
    uint64_t avail_mb = 0;
};

struct ThermalSensor {
    std::string sensor;
    double temp_c = 0.0;
};

struct NetworkInterface {
    std::string ifname;
    LinkState link = LinkState::Unknown;
    std::optional<std::string> ip;
    uint64_t rx_err = 0;
    uint64_t tx_err = 0;
};

struct ResourcesStatus {
    uint32_t sample_window_sec = 60;
    CpuLoad cpu;
    MemoryUsage memory;
    std::vector<StorageMount> storage;
    std::vector<ThermalSensor> thermal;
    std::vector<NetworkInterface> network;
};

struct NtpStatus {
    bool enabled = false;
    std::optional<TimeSyncState> state;
    std::optional<std::chrono::system_clock::time_point> last_sync_at;
};

struct TimeSyncStatus {
    Severity overall = Severity::Unknown;
    TimeSyncSource source = TimeSyncSource::None;
    NtpStatus ntp;
};

struct LastUpdate {
    std::string id;
    std::optional<std::chrono::system_clock::time_point> installed_at;
    UpdateResult result = UpdateResult::Unknown;
    std::optional<std::string> detail;
};

struct UpdateStatus {
    Severity overall = Severity::Unknown;
    std::optional<std::string> active_slot;
    std::optional<LastUpdate> last_update;
};

struct SnapshotSummary {
    Severity severity = Severity::Unknown;
    std::vector<std::string> reasons;
    std::optional<std::string> notes;
};

// -----------------------------------------------------------------------------
// Top-level snapshot state (matches edge.health.state schema)
// -----------------------------------------------------------------------------

struct SnapshotState {
    // Schema metadata
    static constexpr std::string_view schema = "edge.health.state";
    static constexpr std::string_view schema_version = "1.0";
    std::chrono::system_clock::time_point generated_at;

    // Content
    DeviceInfo device;
    BootStatus boot;
    ServicesStatus services;
    ResourcesStatus resources;
    TimeSyncStatus time_sync;
    UpdateStatus update;
    SnapshotSummary summary;
};

} // namespace edge
