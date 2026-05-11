// SPDX-License-Identifier: MIT
// edge-healthd: Data probes
//
// This file contains:
// - ProbeError: Error type for probe failures
// - ProbeResult<T>: Result type (std::expected)
// - Probe concept: Interface all probes must satisfy
// - Concrete probe classes: Device, Boot, Services, Resources, TimeSync, Update

#pragma once

#include "types.hpp"

#include <chrono>
#include <concepts>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
} // namespace sdbus

namespace edge {

 class NetlinkMonitor; // forwared declaration
// Forward declaration
struct Config;

// -----------------------------------------------------------------------------
// Error type for probe operations
// -----------------------------------------------------------------------------

struct ProbeError {
    std::string probe;   // Name of the probe that failed
    std::string message;     // Human-readable error description
    int code = 0;            // Optional error code (errno, etc.)

    [[nodiscard]] std::string what() const {
        if (code != 0) {
            return probe + ": " + message + " (code " + std::to_string(code) + ")";
        }
        return probe + ": " + message;
    }
};

// -----------------------------------------------------------------------------
// Result type for probe operations
// -----------------------------------------------------------------------------

template <typename T>
using ProbeResult = std::expected<T, ProbeError>;

// -----------------------------------------------------------------------------
// Helper to create errors
// -----------------------------------------------------------------------------

inline ProbeError make_error(std::string_view probe,
                                  std::string_view message,
                                  int code = 0) {
    return ProbeError{
        .probe = std::string(probe),
        .message = std::string(message),
        .code = code
    };
}

// -----------------------------------------------------------------------------
// Probe concept
//
// All probes must:
// - Define a DataType alias for the data they collect
// - Provide a collect() method returning ProbeResult<DataType>
// -----------------------------------------------------------------------------

template <typename T>
concept Probe = requires(const T probe) {
    typename T::DataType;
    { probe.collect() } -> std::same_as<ProbeResult<typename T::DataType>>;
};

// -----------------------------------------------------------------------------
// DeviceProbe
// Gathers: device ID, hostname, platform, architecture, OS info
// Sources: /etc/os-release, /etc/machine-id, uname
// -----------------------------------------------------------------------------

class DeviceProbe {
public:
    using DataType = DeviceInfo;

    explicit DeviceProbe(const Config& config);

    [[nodiscard]] ProbeResult<DataType> collect() const;

private:
    const Config& config_;

    [[nodiscard]] OsInfo read_os_release() const;
    [[nodiscard]] std::string read_machine_id() const;
    [[nodiscard]] std::string read_hostname() const;
};

// -----------------------------------------------------------------------------
// BootProbe
// Gathers: boot ID, uptime, boot success/failure tracking
// Sources: clock_gettime(CLOCK_BOOTTIME), systemd boot ID, persistent state
// -----------------------------------------------------------------------------

class BootProbe {
public:
    using DataType = BootStatus;

    explicit BootProbe(
        const Config& config,
        std::filesystem::path state_dir = "/data/edge/health"
    );

    [[nodiscard]] ProbeResult<DataType> collect() const;

    // Called by daemon after successful startup to mark boot OK
    void mark_boot_success();

private:
    const Config& config_;
    std::filesystem::path state_dir_;

    struct BootState {
        std::string last_boot_id;
        uint32_t consecutive_failures = 0;
        bool last_boot_ok = true;
    };

    [[nodiscard]] BootState load_boot_state() const;
    void save_boot_state(const BootState& state) const;
    [[nodiscard]] std::string read_boot_id() const;
    [[nodiscard]] std::chrono::seconds read_uptime() const;
};

// -----------------------------------------------------------------------------
// ServicesProbe
// Gathers: Status of monitored systemd units
// Sources: systemd D-Bus API (org.freedesktop.systemd1)
// -----------------------------------------------------------------------------

class ServicesProbe {
public:
    using DataType = ServicesStatus;

    explicit ServicesProbe(
        const Config& config,
        sdbus::IConnection* dbus = nullptr,
        std::span<const std::string> monitored_units = {}
    );

    [[nodiscard]] ProbeResult<DataType> collect() const;

private:
    const Config& config_;
    sdbus::IConnection* dbus_;
    std::vector<std::string> monitored_units_;

    [[nodiscard]] ServiceUnit query_unit(const std::string& unit_name,
                                         sdbus::IConnection& connection) const;
    [[nodiscard]] Severity evaluate_unit_severity(const ServiceUnit& unit) const;
};

// -----------------------------------------------------------------------------
// ResourcesProbe
// Gathers: CPU load, memory, storage, thermal sensors, network interfaces
// Sources: getloadavg(), sysinfo(), statvfs, /sys/class/thermal, ioctl
// -----------------------------------------------------------------------------

class ResourcesProbe {
public:
    using DataType = ResourcesStatus;

    explicit ResourcesProbe(
        const Config& config,
        NetlinkMonitor& nl_monitor,
        std::span<const std::string> monitored_mounts = {},
        std::span<const std::string> monitored_interfaces = {}
    );

    [[nodiscard]] ProbeResult<DataType> collect() const;

private:
    const Config& config_;
    NetlinkMonitor& nl_monitor_;
    std::vector<std::string> monitored_mounts_;
    std::vector<std::string> monitored_interfaces_;

    [[nodiscard]] CpuLoad collect_cpu_load() const;
    [[nodiscard]] ProbeResult<MemoryUsage> collect_memory() const;
    [[nodiscard]] std::vector<StorageMount> collect_storage() const;
    [[nodiscard]] std::vector<ThermalSensor> collect_thermal() const;
    [[nodiscard]] std::vector<NetworkInterface> collect_network() const;
};

// -----------------------------------------------------------------------------
// TimeSyncProbe
// Gathers: NTP synchronization status, RTC presence and battery health
// Sources: org.freedesktop.timedate1 (D-Bus), /sys/class/rtc/<dev>/ (sysfs)
// -----------------------------------------------------------------------------

class TimeSyncProbe {
public:
    using DataType = TimeSyncStatus;

    explicit TimeSyncProbe(const Config& config, sdbus::IConnection* dbus = nullptr);

    [[nodiscard]] ProbeResult<DataType> collect() const;

private:
    const Config& config_;
    sdbus::IConnection* dbus_;

    [[nodiscard]] NtpStatus collect_ntp() const;
    [[nodiscard]] RtcStatus collect_rtc() const;
    [[nodiscard]] Severity evaluate_sync_severity(const TimeSyncStatus& status) const;
};

// -----------------------------------------------------------------------------
// UpdateProbe
// Gathers: Software version, active slot (A/B), last update result
// Sources: custom state file (future: RAUC D-Bus)
// -----------------------------------------------------------------------------

class UpdateProbe {
public:
    using DataType = UpdateStatus;

    explicit UpdateProbe(
        const Config& config,
        sdbus::IConnection* dbus = nullptr,
        std::filesystem::path state_dir = "/data/edge/update"
    );

    [[nodiscard]] ProbeResult<DataType> collect() const;

private:
    const Config& config_;
    sdbus::IConnection* dbus_;
    std::filesystem::path state_dir_;

    [[nodiscard]] std::optional<std::string> detect_active_slot() const;
    [[nodiscard]] std::optional<LastUpdate> load_last_update() const;
    // Queries de.pengutronix.rauc D-Bus; returns true and populates status if RAUC is available.
    [[nodiscard]] bool collect_rauc_update(UpdateStatus& status) const;
};

// -----------------------------------------------------------------------------
// JournalProbe
// Gathers: System-wide critical/error journal entries (Priority <= 3)
// Sources: systemd journal (sd_journal)
// -----------------------------------------------------------------------------

class JournalProbe {
public:
    using DataType = JournalStatus;

    explicit JournalProbe(const Config& config);

    [[nodiscard]] ProbeResult<JournalStatus> collect() const;

private:
    const Config& config_;
};

// -----------------------------------------------------------------------------
// CrashProbe
// Gathers: persisted crash artifacts from systemd-pstore output
// Sources: /var/lib/systemd/pstore
// -----------------------------------------------------------------------------

class CrashProbe {
public:
    using DataType = CrashStatus;

    explicit CrashProbe(
        const Config& config,
        std::filesystem::path state_dir = "/data/edge/health",
        std::filesystem::path pstore_dir = "/var/lib/systemd/pstore"
    );

    [[nodiscard]] ProbeResult<CrashStatus> collect() const;

private:
    const Config& config_;
    std::filesystem::path state_dir_;
    std::filesystem::path pstore_dir_;
};

// -----------------------------------------------------------------------------
// Concept compliance verification
// -----------------------------------------------------------------------------

static_assert(Probe<DeviceProbe>);
static_assert(Probe<BootProbe>);
static_assert(Probe<ServicesProbe>);
static_assert(Probe<ResourcesProbe>);
static_assert(Probe<TimeSyncProbe>);
static_assert(Probe<UpdateProbe>);
static_assert(Probe<JournalProbe>);
static_assert(Probe<CrashProbe>);

} // namespace edge
