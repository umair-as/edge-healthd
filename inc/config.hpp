// SPDX-License-Identifier: MIT
// edge-healthd: Configuration
// Declarative configuration loaded from file or defaults

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace edge {

// -----------------------------------------------------------------------------
// Threshold configuration for severity evaluation
// -----------------------------------------------------------------------------

struct Thresholds {
    // Resource thresholds (percentage or absolute)
    uint8_t cpu_load_warn = 80;           // 1-min load avg as % of cores
    uint8_t cpu_load_crit = 95;
    uint8_t mem_used_warn = 80;           // Memory used %
    uint8_t mem_used_crit = 95;
    uint8_t disk_used_warn = 80;          // Disk used %
    uint8_t disk_used_crit = 95;
    double temp_warn_c = 70.0;            // Temperature
    double temp_crit_c = 85.0;

    // Service restart thresholds
    uint32_t service_restart_warn = 3;    // Restarts since boot
    uint32_t service_restart_crit = 10;

    // Boot tracking
    uint32_t boot_fail_warn = 1;
    uint32_t boot_fail_crit = 3;

    // PTP offset thresholds (nanoseconds) — reserved for future PTP probe
    uint32_t ptp_offset_warn_ns = 10000;
    uint32_t ptp_offset_crit_ns = 100000;

    // RTC battery voltage thresholds (millivolts)
    // CR2032/supercap nominal ~3000 mV; warn at ~10% drop, crit at ~17% drop
    uint32_t rtc_voltage_warn_mv = 2700;
    uint32_t rtc_voltage_crit_mv = 2500;
};

// -----------------------------------------------------------------------------
// Main configuration structure
// -----------------------------------------------------------------------------

struct Config {
    // ---------------------------------------------------------------------
    // Identity
    // ---------------------------------------------------------------------
    std::string device_id;                              // Unique device identifier
    std::string platform;                               // Hardware platform name

    // ---------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------
    std::filesystem::path snapshot_file = "/run/health/state.json";
    std::filesystem::path state_dir = "/data/edge/health";
    std::filesystem::path config_file = "/etc/edge/healthd.conf";

    // ---------------------------------------------------------------------
    // Collection settings
    // ---------------------------------------------------------------------
    std::chrono::seconds collect_interval{60};          // How often to collect
    uint32_t sample_window_sec = 60;                    // Resource averaging window
    std::chrono::seconds time_sync_interval{300};       // How often to query timedate1 (decoupled from collect_interval)
    std::chrono::seconds update_check_interval{1800};   // How often to query RAUC D-Bus (updates are rare events)

    // ---------------------------------------------------------------------
    // Monitored items (empty = auto-detect or skip)
    // ---------------------------------------------------------------------
    std::vector<std::string> monitored_services = {
        "sshd.socket",
        "NetworkManager.service"
    };

    std::vector<std::string> monitored_mounts = {
        "/",
        "/data"
    };

    std::vector<std::string> monitored_interfaces = {
        "eth0",
        "eth1"
    };

    // ---------------------------------------------------------------------
    // Feature flags
    // ---------------------------------------------------------------------
    bool enable_ntp = true;                             // Monitor NTP sync
    bool enable_ptp = false;                            // PTP config present; no probe implemented yet
    bool enable_rtc = true;                             // Monitor RTC presence and battery health
    std::filesystem::path rtc_device = "/sys/class/rtc/rtc0";
    bool enable_thermal = true;                         // Monitor temperature
    bool enable_update_tracking = true;                 // Track update status

    // ---------------------------------------------------------------------
    // Severity evaluation
    // ---------------------------------------------------------------------
    Thresholds thresholds;

    // ---------------------------------------------------------------------
    // D-Bus settings
    // ---------------------------------------------------------------------
    std::chrono::milliseconds dbus_timeout{2000};      // Method call timeout

    // ---------------------------------------------------------------------
    // Runtime options
    // ---------------------------------------------------------------------
    bool foreground = false;                            // Don't daemonize
    std::string log_level = "info";                     // debug/info/warn/error
    bool verbose = false;                               // Debug logging (CLI override)

    // ---------------------------------------------------------------------
    // Log excerpt collection settings
    // ---------------------------------------------------------------------
    // How many lines to include per unit (default 20)
    uint32_t log_excerpt_max_lines = 20;

    // Minimum priority to include in excerpts. If not set, include all.
    // Accepts syslog priority names ("emerg","alert","crit","err",
    // "warning","notice","info","debug") when loaded from JSON.
    std::optional<int> log_excerpt_min_priority; // 0 (emerg) .. 7 (debug)

    // Time window (seconds) to include log messages from (rolling back from now).
    // If not set, include all available messages.
    std::optional<uint64_t> log_excerpt_window_sec;

    // Wall-clock budget for the entire journal scan (open + seek + iterate).
    // If exceeded the probe returns whatever entries were collected so far and
    // logs a warning.  Protects the collection cycle against slow/large journals.
    std::chrono::milliseconds journal_scan_timeout{3000};

    // ---------------------------------------------------------------------
    // Factory methods
    // ---------------------------------------------------------------------

    /// Load configuration from file, falling back to defaults
    [[nodiscard]] static Config load(const std::filesystem::path& path);

    /// Load configuration from file, or return defaults if file doesn't exist
    [[nodiscard]] static Config load_or_default(const std::filesystem::path& path);

    /// Return default configuration
    [[nodiscard]] static Config defaults();

    /// Validate configuration, return error message if invalid
    [[nodiscard]] std::optional<std::string> validate() const;

    /// Serialize configuration to JSON for diagnostics.
    [[nodiscard]] std::string to_json_string(int indent = 2) const;
};

// -----------------------------------------------------------------------------
// Configuration file format (for documentation)
// -----------------------------------------------------------------------------
//
// File: /etc/edge/healthd.conf (JSON)
//
// {
//   "device_id": "vf2-001",
//   "platform": "visionfive2",
//   "snapshot_file": "/run/health/state.json",
//   "state_dir": "/data/edge/health",
//   "collect_interval_sec": 60,
//   "time_sync_interval_sec": 300,
//   "log_level": "info",
//   "dbus_timeout_ms": 2000,
//   "monitored_services": ["sshd.socket", "NetworkManager.service"],
//   "monitored_mounts": ["/", "/data"],
//   "monitored_interfaces": ["eth0"],
//   "thresholds": {
//     "cpu_load_warn": 80,
//     "mem_used_crit": 95,
//   }
// }
//
// -----------------------------------------------------------------------------

} // namespace edge
