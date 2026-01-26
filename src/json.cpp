// SPDX-License-Identifier: MIT
// edge-healthd: JSON serialization

#include "json.hpp"

#include <iomanip>
#include <sstream>

namespace edge {

namespace {

// Format time_point as ISO 8601 string
std::string format_time(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&time_t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // namespace

void to_json(nlohmann::json& j, const OsInfo& os) {
    j = nlohmann::json{
        {"distro", os.distro},
        {"version", os.version},
        {"build_id", os.build_id},
        {"kernel", os.kernel}
    };
}

void to_json(nlohmann::json& j, const DeviceInfo& device) {
    j = nlohmann::json{
        {"device_id", device.device_id},
        {"hostname", device.hostname},
        {"platform", device.platform},
        {"arch", device.arch},
        {"os", device.os}
    };
}

void to_json(nlohmann::json& j, const BootStatus& boot) {
    j = nlohmann::json{
        {"boot_id", boot.boot_id},
        {"last_boot_at", format_time(boot.last_boot_at)},
        {"uptime", boot.uptime.count()},
        {"boot_ok", boot.boot_ok},
        {"boot_fail_count", boot.boot_fail_count}
    };

    if (boot.last_reboot_reason) {
        j["last_reboot_reason"] = *boot.last_reboot_reason;
    }
}

void to_json(nlohmann::json& j, const ServiceUnit& unit) {
    j = nlohmann::json{
        {"name", unit.name},
        {"state", std::string(edge::to_string(unit.state))},
        {"severity", std::string(edge::to_string(unit.severity))},
        {"restart_count", unit.restart_count}
    };

    if (unit.since) {
        j["since"] = format_time(*unit.since);
    }
    if (unit.result) {
        j["result"] = *unit.result;
    }
    if (unit.detail) {
        j["detail"] = *unit.detail;
    }
}

void to_json(nlohmann::json& j, const ServicesStatus& services) {
    j = nlohmann::json{
        {"overall", std::string(edge::to_string(services.overall))},
        {"units", services.units}
    };
}

void to_json(nlohmann::json& j, const CpuLoad& cpu) {
    j = nlohmann::json{
        {"load1", cpu.load1},
        {"load5", cpu.load5},
        {"load15", cpu.load15}
    };
}

void to_json(nlohmann::json& j, const MemoryUsage& memory) {
    j = nlohmann::json{
        {"mem_total_mb", memory.mem_total_mb},
        {"mem_used_mb", memory.mem_used_mb},
        {"swap_used_mb", memory.swap_used_mb}
    };
}

void to_json(nlohmann::json& j, const StorageMount& mount) {
    j = nlohmann::json{
        {"mount", mount.mount},
        {"fs", mount.fs},
        {"used_pct", mount.used_pct},
        {"avail_mb", mount.avail_mb}
    };
}

void to_json(nlohmann::json& j, const ThermalSensor& sensor) {
    j = nlohmann::json{
        {"sensor", sensor.sensor},
        {"temp_c", sensor.temp_c}
    };
}

void to_json(nlohmann::json& j, const NetworkInterface& iface) {
    j = nlohmann::json{
        {"ifname", iface.ifname},
        {"link", std::string(edge::to_string(iface.link))},
        {"rx_err", iface.rx_err},
        {"tx_err", iface.tx_err}
    };

    if (iface.ip) {
        j["ip"] = *iface.ip;
    }
}

void to_json(nlohmann::json& j, const ResourcesStatus& resources) {
    j = nlohmann::json{
        {"sample_window_sec", resources.sample_window_sec},
        {"cpu", resources.cpu},
        {"memory", resources.memory},
        {"storage", resources.storage},
        {"thermal", resources.thermal},
        {"network", resources.network}
    };
}

void to_json(nlohmann::json& j, const NtpStatus& ntp) {
    j = nlohmann::json{
        {"enabled", ntp.enabled}
    };

    if (ntp.state) {
        j["state"] = std::string(edge::to_string(*ntp.state));
    }
    if (ntp.last_sync_at) {
        j["last_sync_at"] = format_time(*ntp.last_sync_at);
    }
}

void to_json(nlohmann::json& j, const TimeSyncStatus& time_sync) {
    j = nlohmann::json{
        {"overall", std::string(edge::to_string(time_sync.overall))},
        {"source", std::string(edge::to_string(time_sync.source))},
        {"ntp", time_sync.ntp}
    };
}

void to_json(nlohmann::json& j, const LastUpdate& update) {
    j = nlohmann::json{
        {"id", update.id},
        {"result", std::string(edge::to_string(update.result))}
    };

    if (update.installed_at) {
        j["installed_at"] = format_time(*update.installed_at);
    }
    if (update.detail) {
        j["detail"] = *update.detail;
    }
}

void to_json(nlohmann::json& j, const UpdateStatus& update) {
    j = nlohmann::json{
        {"overall", std::string(edge::to_string(update.overall))}
    };

    if (update.active_slot) {
        j["active_slot"] = *update.active_slot;
    }
    if (update.last_update) {
        j["last_update"] = *update.last_update;
    }
}

void to_json(nlohmann::json& j, const SnapshotSummary& summary) {
    j = nlohmann::json{
        {"severity", std::string(edge::to_string(summary.severity))},
        {"reasons", summary.reasons}
    };

    if (summary.notes) {
        j["notes"] = *summary.notes;
    }
}

void to_json(nlohmann::json& j, const SnapshotState& state) {
    j = nlohmann::json{
        {"schema", std::string(SnapshotState::schema)},
        {"schema_version", std::string(SnapshotState::schema_version)},
        {"generated_at", format_time(state.generated_at)},
        {"device", state.device},
        {"boot", state.boot},
        {"services", state.services},
        {"resources", state.resources},
        {"time_sync", state.time_sync},
        {"update", state.update},
        {"summary", state.summary}
    };
}

namespace json {

std::string serialize(const SnapshotState& state) {
    nlohmann::json j = state;
    return j.dump();
}

std::string serialize_pretty(const SnapshotState& state, int indent) {
    nlohmann::json j = state;
    return j.dump(indent);
}

} // namespace json

} // namespace edge
