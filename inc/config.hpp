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
    std::filesystem::path snapshot_file = "/data/edge/health/state.json";
    std::filesystem::path state_dir = "/data/edge/health";
    std::filesystem::path config_file = "/etc/edge/healthd.conf";

    // ---------------------------------------------------------------------
    // Collection settings
    // ---------------------------------------------------------------------
    std::chrono::seconds collect_interval{60};          // How often to collect
    uint32_t sample_window_sec = 60;                    // Resource averaging window

    // ---------------------------------------------------------------------
    // Monitored items (empty = auto-detect or skip)
    // ---------------------------------------------------------------------
    std::vector<std::string> monitored_services = {
        "sshd.service",
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
//   "snapshot_file": "/data/edge/health/state.json",
//   "state_dir": "/data/edge/health",
//   "collect_interval_sec": 60,
//   "log_level": "info",
//   "dbus_timeout_ms": 2000,
//   "monitored_services": ["sshd.service", "NetworkManager.service"],
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
