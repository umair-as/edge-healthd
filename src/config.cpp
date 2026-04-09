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

// Map syslog priority name to numeric value (0..7). Returns -1 on unknown.
int priority_from_string(const std::string& p) {
    std::string s;
    s.reserve(p.size());
    for (char ch : p) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    if (s == "emerg" || s == "panic") return 0;
    if (s == "alert") return 1;
    if (s == "crit" || s == "critical") return 2;
    if (s == "err" || s == "error") return 3;
    if (s == "warning" || s == "warn") return 4;
    if (s == "notice") return 5;
    if (s == "info" || s == "informational") return 6;
    if (s == "debug") return 7;
    return -1;
}
} // namespace

namespace edge {

Config Config::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open config file: " + path.string());
    }

    // ignore_comments=true enables // and /* */ style comments in the JSON file,
    // matching the sshd_config / unix convention operators expect.
    auto json = nlohmann::json::parse(file, nullptr, true, true);
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
    if (json.contains("time_sync_interval_sec")) {
        config.time_sync_interval = std::chrono::seconds(
            json["time_sync_interval_sec"].get<int>());
    }
    if (json.contains("update_check_interval_sec")) {
        config.update_check_interval = std::chrono::seconds(
            json["update_check_interval_sec"].get<int>());
    }
    if (json.contains("trigger_min_interval_sec")) {
        config.trigger_min_interval = std::chrono::seconds(
            json["trigger_min_interval_sec"].get<int>());
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
    if (json.contains("enable_ptp")) {
        config.enable_ptp = json["enable_ptp"].get<bool>();
    }
    if (json.contains("enable_rtc")) {
        config.enable_rtc = json["enable_rtc"].get<bool>();
    }
    if (json.contains("rtc_device")) {
        config.rtc_device = json["rtc_device"].get<std::string>();
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

    // Log excerpt options
    if (json.contains("log_excerpt")) {
        auto& le = json["log_excerpt"];
        if (le.contains("max_lines")) {
            config.log_excerpt_max_lines = le["max_lines"].get<uint32_t>();
        }
        if (le.contains("min_priority")) {
            if (le["min_priority"].is_string()) {
                int v = priority_from_string(le["min_priority"].get<std::string>());
                if (v >= 0) config.log_excerpt_min_priority = v;
            } else if (le["min_priority"].is_number_integer()) {
                int v = le["min_priority"].get<int>();
                if (v >= 0 && v <= 7) config.log_excerpt_min_priority = v;
            }
        }
        if (le.contains("window_sec")) {
            config.log_excerpt_window_sec = le["window_sec"].get<uint64_t>();
        }
        if (le.contains("scan_timeout_ms")) {
            config.journal_scan_timeout =
                std::chrono::milliseconds(le["scan_timeout_ms"].get<int>());
        }
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
        if (t.contains("ptp_offset_warn_ns")) {
            config.thresholds.ptp_offset_warn_ns = t["ptp_offset_warn_ns"].get<uint32_t>();
        }
        if (t.contains("ptp_offset_crit_ns")) {
            config.thresholds.ptp_offset_crit_ns = t["ptp_offset_crit_ns"].get<uint32_t>();
        }
        if (t.contains("rtc_voltage_warn_mv")) {
            config.thresholds.rtc_voltage_warn_mv = t["rtc_voltage_warn_mv"].get<uint32_t>();
        }
        if (t.contains("rtc_voltage_crit_mv")) {
            config.thresholds.rtc_voltage_crit_mv = t["rtc_voltage_crit_mv"].get<uint32_t>();
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

    if (time_sync_interval.count() < 1) {
        return "time_sync_interval_sec must be at least 1";
    }

    if (update_check_interval.count() < 1) {
        return "update_check_interval_sec must be at least 1";
    }

    if (trigger_min_interval.count() < 0) {
        return "trigger_min_interval_sec must be >= 0 (0 disables rate-limiting)";
    }

    if (!is_valid_log_level(log_level)) {
        return "log_level must be one of: debug, info, warn, error";
    }

    if (dbus_timeout.count() < 1) {
        return "dbus_timeout_ms must be at least 1";
    }

    if (log_excerpt_max_lines == 0 || log_excerpt_max_lines > 1000) {
        return "log_excerpt.max_lines must be between 1 and 1000";
    }

    if (journal_scan_timeout.count() < 100) {
        return "log_excerpt.scan_timeout_ms must be at least 100";
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

std::string Config::to_json_string(int indent) const {
    nlohmann::json json;

    // Identity
    json["device_id"] = device_id;
    json["platform"] = platform;

    // Paths
    json["snapshot_file"] = snapshot_file.string();
    json["state_dir"] = state_dir.string();

    // Collection settings
    json["collect_interval_sec"] = collect_interval.count();
    json["sample_window_sec"] = sample_window_sec;
    json["time_sync_interval_sec"] = time_sync_interval.count();
    json["update_check_interval_sec"] = update_check_interval.count();
    json["trigger_min_interval_sec"] = trigger_min_interval.count();

    // Runtime options (file-visible)
    json["log_level"] = log_level;
    json["dbus_timeout_ms"] = dbus_timeout.count();

    // Monitored items
    json["monitored_services"] = monitored_services;
    json["monitored_mounts"] = monitored_mounts;
    json["monitored_interfaces"] = monitored_interfaces;

    // Feature flags
    json["enable_ntp"] = enable_ntp;
    json["enable_ptp"] = enable_ptp;
    json["enable_rtc"] = enable_rtc;
    json["rtc_device"] = rtc_device.string();
    json["enable_thermal"] = enable_thermal;
    json["enable_update_tracking"] = enable_update_tracking;

    // Thresholds
    auto& t = json["thresholds"];
    t["cpu_load_warn"] = thresholds.cpu_load_warn;
    t["cpu_load_crit"] = thresholds.cpu_load_crit;
    t["mem_used_warn"] = thresholds.mem_used_warn;
    t["mem_used_crit"] = thresholds.mem_used_crit;
    t["disk_used_warn"] = thresholds.disk_used_warn;
    t["disk_used_crit"] = thresholds.disk_used_crit;
    t["temp_warn_c"] = thresholds.temp_warn_c;
    t["temp_crit_c"] = thresholds.temp_crit_c;
    t["service_restart_warn"] = thresholds.service_restart_warn;
    t["service_restart_crit"] = thresholds.service_restart_crit;
    t["boot_fail_warn"] = thresholds.boot_fail_warn;
    t["boot_fail_crit"] = thresholds.boot_fail_crit;
    t["ptp_offset_warn_ns"] = thresholds.ptp_offset_warn_ns;
    t["ptp_offset_crit_ns"] = thresholds.ptp_offset_crit_ns;
    t["rtc_voltage_warn_mv"] = thresholds.rtc_voltage_warn_mv;
    t["rtc_voltage_crit_mv"] = thresholds.rtc_voltage_crit_mv;

    // Log excerpt settings
    auto& le = json["log_excerpt"];
    le["max_lines"] = log_excerpt_max_lines;
    if (log_excerpt_min_priority) le["min_priority"] = *log_excerpt_min_priority;
    if (log_excerpt_window_sec) le["window_sec"] = *log_excerpt_window_sec;
    le["scan_timeout_ms"] = journal_scan_timeout.count();

    return json.dump(indent);
}

} // namespace edge
