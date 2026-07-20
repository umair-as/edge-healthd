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

enum class Duplex { Full, Half, Unknown };

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

[[nodiscard]] constexpr std::string_view to_string(Duplex d) noexcept {
    switch (d) {
        case Duplex::Full:    return "full";
        case Duplex::Half:    return "half";
        case Duplex::Unknown: return "unknown";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// Data structures (leaf types first, composites later)
// -----------------------------------------------------------------------------

struct OsInfo {
    std::string distro = "unknown";
    std::string version ="";
    std::string build_id=  "";
    std::string kernel  = "unknown";
};

struct DeviceInfo {
    std::string device_id = "unknown";
    std::string hostname = "unknown";
    std::string platform = "unknown";      // e.g., "visionfive2", "rpi5", "imx93"
    std::string arch = "uknown";         // e.g., "riscv64", "aarch64"
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
    std::vector<std::string> log_excerpt; // max 20 entries, each <= 512 chars
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
    bool carrier = false;  // Changed from std::optional<bool> to bool
    std::optional<std::string> ip;
    
    // Statistics
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    uint64_t rx_packets = 0;
    uint64_t tx_packets = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_dropped = 0;
    uint64_t rx_err = 0;
    uint64_t tx_err = 0;
    
    // Link metadata (from IFLA_* — always available via netlink)
    uint32_t mtu = 0;
    std::string mac;            // "aa:bb:cc:dd:ee:ff"
    std::string operstate;      // "up", "down", "unknown", etc.
    uint32_t carrier_changes = 0;
    uint32_t carrier_up_count = 0;
    uint32_t carrier_down_count = 0;

    // Hardware info (from Ethtool Netlink — may not be available)
    std::optional<uint32_t> speed_mbps;
    std::optional<Duplex> duplex;
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

struct PtpStatus {
    bool enabled = false;
    std::optional<std::string> interface;
    std::optional<int64_t> offset_ns;
    std::optional<uint64_t> rms_ns;
    std::optional<TimeSyncState> state;
    std::optional<std::chrono::system_clock::time_point> last_sync_at;
    std::optional<std::string> role; // keep as string for v1
};

struct RtcStatus {
    bool enabled = false;
    bool hctosys = false;                    // RTC was used to set system clock at boot
    std::optional<uint32_t> voltage_mv;      // backup battery voltage in millivolts
    std::optional<int64_t> drift_sec;        // RTC vs system clock skew (seconds)
};

struct TimeSyncStatus {
    Severity overall = Severity::Unknown;
    TimeSyncSource source = TimeSyncSource::None;
    NtpStatus ntp;
    PtpStatus ptp;
    RtcStatus rtc;
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

struct JournalStatus {
    Severity overall = Severity::Unknown;
    uint32_t error_count = 0;               // entries collected in scan window
    std::vector<std::string> recent_errors; // newest first, capped by config
};

struct CrashArtifact {
    std::string name;
    uint64_t size_bytes = 0;
    std::optional<std::chrono::system_clock::time_point> mtime;
    // Classification by filename: "panic" for kernel-fault dumps (dmesg-*, EFI
    // dump-*) or "informational" for benign records (console-ramoops-*, pmsg-*,
    // ftrace-*, anything else). Only "panic" artifacts drive severity/alarm.
    std::string kind;
};

struct CrashStatus {
    bool present = false;              // any pstore artifact exists (all kinds)
    std::optional<std::string> source; // "pstore"
    std::optional<std::chrono::system_clock::time_point> last_panic_at;
    std::optional<std::string> fingerprint; // computed over PANIC artifacts only
    uint32_t artifact_count = 0;       // total artifacts (all kinds)
    std::vector<CrashArtifact> artifacts;
    bool acknowledged = false;         // stored fingerprint matches panic fingerprint
    // Trustworthiness lifecycle (M1): severity keys off kernel-fault dumps only,
    // aged by boot. panic_count counts "panic"-kind artifacts; panic_current_boot
    // is true when at least one panic artifact was captured during the current
    // boot (mtime >= boot wall-clock start).
    uint32_t panic_count = 0;
    bool panic_current_boot = false;
};

struct SnapshotSummary {
    Severity severity = Severity::Unknown;
    std::vector<std::string> reasons{"initial"};
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
    uint64_t cycle = 0;  // Monotonic counter incremented on every collection cycle.
                         // Consumers can use this to confirm the daemon is collecting
                         // without relying on the rate-limited "Snapshot collected" log.

    // Content
    DeviceInfo device;
    BootStatus boot;
    ServicesStatus services;
    ResourcesStatus resources;
    TimeSyncStatus time_sync;
    UpdateStatus update;
    JournalStatus journal;
    CrashStatus crash;
    SnapshotSummary summary;
};

} // namespace edge
