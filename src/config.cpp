// SPDX-License-Identifier: MIT
// edge-healthd: Configuration implementation

#include "config.hpp"

#include <nlohmann/json.hpp>
#include <cctype>
#include <fstream>

namespace {
std::string normalize_log_level(const std::string& level) {
    std::string normalized;
    normalized.reserve(level.size());
    for (char ch : level) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

bool is_valid_log_level(const std::string& level) {
    return level == "debug" || level == "info" || level == "warn" || level == "error";
}
} // namespace

namespace edge {

Config Config::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open config file: " + path.string());
    }

    auto json = nlohmann::json::parse(file);
    Config config = defaults();

    // Identity
    if (json.contains("device_id")) {
        config.device_id = json["device_id"].get<std::string>();
    }
    if (json.contains("platform")) {
        config.platform = json["platform"].get<std::string>();
    }

    // Paths
    if (json.contains("snapshot_file")) {
        config.snapshot_file = json["snapshot_file"].get<std::string>();
    }
    if (json.contains("state_dir")) {
        config.state_dir = json["state_dir"].get<std::string>();
    }

    // Collection settings
    if (json.contains("collect_interval_sec")) {
        config.collect_interval = std::chrono::seconds(
            json["collect_interval_sec"].get<int>());
    }
    if (json.contains("sample_window_sec")) {
        config.sample_window_sec = json["sample_window_sec"].get<uint32_t>();
    }

    // Monitored items
    if (json.contains("monitored_services")) {
        config.monitored_services =
            json["monitored_services"].get<std::vector<std::string>>();
    }
    if (json.contains("monitored_mounts")) {
        config.monitored_mounts =
            json["monitored_mounts"].get<std::vector<std::string>>();
    }
    if (json.contains("monitored_interfaces")) {
        config.monitored_interfaces =
            json["monitored_interfaces"].get<std::vector<std::string>>();
    }

    // Feature flags
    if (json.contains("enable_ntp")) {
        config.enable_ntp = json["enable_ntp"].get<bool>();
    }
    if (json.contains("enable_thermal")) {
        config.enable_thermal = json["enable_thermal"].get<bool>();
    }
    if (json.contains("enable_update_tracking")) {
        config.enable_update_tracking = json["enable_update_tracking"].get<bool>();
    }

    if (json.contains("log_level")) {
        config.log_level = normalize_log_level(json["log_level"].get<std::string>());
    }
    if (json.contains("dbus_timeout_ms")) {
        config.dbus_timeout = std::chrono::milliseconds(
            json["dbus_timeout_ms"].get<int>());
    }

    // Thresholds
    if (json.contains("thresholds")) {
        auto& t = json["thresholds"];
        if (t.contains("cpu_load_warn")) {
            config.thresholds.cpu_load_warn = t["cpu_load_warn"].get<uint8_t>();
        }
        if (t.contains("cpu_load_crit")) {
            config.thresholds.cpu_load_crit = t["cpu_load_crit"].get<uint8_t>();
        }
        if (t.contains("mem_used_warn")) {
            config.thresholds.mem_used_warn = t["mem_used_warn"].get<uint8_t>();
        }
        if (t.contains("mem_used_crit")) {
            config.thresholds.mem_used_crit = t["mem_used_crit"].get<uint8_t>();
        }
        if (t.contains("disk_used_warn")) {
            config.thresholds.disk_used_warn = t["disk_used_warn"].get<uint8_t>();
        }
        if (t.contains("disk_used_crit")) {
            config.thresholds.disk_used_crit = t["disk_used_crit"].get<uint8_t>();
        }
        if (t.contains("temp_warn_c")) {
            config.thresholds.temp_warn_c = t["temp_warn_c"].get<double>();
        }
        if (t.contains("temp_crit_c")) {
            config.thresholds.temp_crit_c = t["temp_crit_c"].get<double>();
        }
        if (t.contains("service_restart_warn")) {
            config.thresholds.service_restart_warn = t["service_restart_warn"].get<uint32_t>();
        }
        if (t.contains("service_restart_crit")) {
            config.thresholds.service_restart_crit = t["service_restart_crit"].get<uint32_t>();
        }
        if (t.contains("boot_fail_warn")) {
            config.thresholds.boot_fail_warn = t["boot_fail_warn"].get<uint32_t>();
        }
        if (t.contains("boot_fail_crit")) {
            config.thresholds.boot_fail_crit = t["boot_fail_crit"].get<uint32_t>();
        }
    }

    return config;
}

Config Config::load_or_default(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        try {
            return load(path);
        } catch (const std::exception&) {
            // Fall through to defaults
        }
    }
    return defaults();
}

Config Config::defaults() {
    return Config{};
}

std::optional<std::string> Config::validate() const {
    if (collect_interval.count() < 1) {
        return "collect_interval must be at least 1 second";
    }

    if (!is_valid_log_level(log_level)) {
        return "log_level must be one of: debug, info, warn, error";
    }

    if (dbus_timeout.count() < 1) {
        return "dbus_timeout_ms must be at least 1";
    }

    if (thresholds.cpu_load_warn >= thresholds.cpu_load_crit) {
        return "cpu_load_warn must be less than cpu_load_crit";
    }

    if (thresholds.mem_used_warn >= thresholds.mem_used_crit) {
        return "mem_used_warn must be less than mem_used_crit";
    }

    if (thresholds.disk_used_warn >= thresholds.disk_used_crit) {
        return "disk_used_warn must be less than disk_used_crit";
    }

    if (thresholds.temp_warn_c >= thresholds.temp_crit_c) {
        return "temp_warn_c must be less than temp_crit_c";
    }

    return std::nullopt;
}

} // namespace edge
