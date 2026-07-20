// SPDX-License-Identifier: MIT
// edge-healthd: Probes (D-Bus) implementation

#include "probes.hpp"
#include "config.hpp"
#include "journal.hpp"
#include "journal_reader.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <sdbus-c++/sdbus-c++.h>
#include <array>
#include <vector>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "log.hpp"
#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#endif

namespace edge {

// -----------------------------------------------------------------------------
// ServicesProbe
// -----------------------------------------------------------------------------

namespace {
bool is_safe_unit_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (char ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) &&
            ch != '.' && ch != '-' && ch != '_' && ch != '@' && ch != ':') {
            return false;
        }
    }
    return true;
}
 

} // namespace

ServicesProbe::ServicesProbe(const Config& config,
                                     sdbus::IConnection* dbus,
                                     std::span<const std::string> monitored_units,
                                     const JournalReader* journal)
    : config_(config)
    , dbus_(dbus)
    , monitored_units_(monitored_units.begin(), monitored_units.end())
    , journal_(journal) {

    // Use config defaults if not specified
    if (monitored_units_.empty()) {
        monitored_units_ = config_.monitored_services;
    }
}

ProbeResult<ServicesStatus> ServicesProbe::collect() const {
    ServicesStatus status;
    status.overall = Severity::Ok;

    if (!dbus_) {
        for (const auto& unit_name : monitored_units_) {
            ServiceUnit unit;
            unit.name = unit_name;
            unit.state = ServiceState::Unknown;
            unit.severity = Severity::Unknown;
            unit.detail = "D-Bus unavailable";
            status.units.push_back(std::move(unit));
        }
        status.overall = Severity::Unknown;
        return status;
    }

    for (const auto& unit_name : monitored_units_) {
        auto unit = query_unit(unit_name, *dbus_);
        unit.severity = evaluate_unit_severity(unit);

        // Update overall severity
        if (unit.severity == Severity::Crit) {
            status.overall = Severity::Crit;
        } else if (unit.severity == Severity::Warn && status.overall != Severity::Crit) {
            status.overall = Severity::Warn;
        }

        status.units.push_back(std::move(unit));
    }

    return status;
}

ServiceUnit ServicesProbe::query_unit(const std::string& unit_name,
                                          sdbus::IConnection& connection) const {
    ServiceUnit unit;
    unit.name = unit_name;
    unit.state = ServiceState::Unknown;
    unit.restart_count = 0;

    if (!is_safe_unit_name(unit_name)) {
        unit.detail = "Invalid unit name";
        return unit;
    }

    try {
        auto manager = sdbus::createProxy(connection,
                                          sdbus::ServiceName("org.freedesktop.systemd1"),
                                          sdbus::ObjectPath("/org/freedesktop/systemd1"));

        sdbus::ObjectPath unit_path;
        manager->callMethod("GetUnit")
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(unit_name)
            .storeResultsTo(unit_path);

        auto unit_proxy = sdbus::createProxy(connection,
                                             sdbus::ServiceName("org.freedesktop.systemd1"),
                                             unit_path);

        auto active_state_variant = unit_proxy->getProperty("ActiveState")
                                        .onInterface("org.freedesktop.systemd1.Unit");
        auto active_state = static_cast<std::string>(active_state_variant);

        // Map state string to enum
        if (active_state == "active") {
            unit.state = ServiceState::Active;
        } else if (active_state == "inactive") {
            unit.state = ServiceState::Inactive;

            // For socket-activated services the .service unit is inactive while
            // the .socket unit is listening — that is healthy, not a warning.
            // If a corresponding .socket unit is active, treat the service as ok.
            static constexpr std::string_view svc_suffix = ".service";
            if (unit_name.size() > svc_suffix.size() &&
                unit_name.compare(unit_name.size() - svc_suffix.size(),
                                  svc_suffix.size(), svc_suffix) == 0) {
                std::string socket_name =
                    unit_name.substr(0, unit_name.size() - svc_suffix.size()) + ".socket";
                try {
                    sdbus::ObjectPath socket_path;
                    manager->callMethod("GetUnit")
                        .onInterface("org.freedesktop.systemd1.Manager")
                        .withArguments(socket_name)
                        .storeResultsTo(socket_path);
                    auto socket_proxy = sdbus::createProxy(
                        connection,
                        sdbus::ServiceName("org.freedesktop.systemd1"),
                        socket_path);
                    auto socket_state_v = socket_proxy->getProperty("ActiveState")
                                             .onInterface("org.freedesktop.systemd1.Unit");
                    if (static_cast<std::string>(socket_state_v) == "active") {
                        unit.state = ServiceState::Active;
                        unit.result = "socket-activated";
                    }
                } catch (const sdbus::Error&) {
                    // No matching .socket unit — leave state as Inactive
                }
            }
        } else if (active_state == "failed") {
            unit.state = ServiceState::Failed;
        } else if (active_state == "activating") {
            unit.state = ServiceState::Activating;
        } else if (active_state == "deactivating") {
            unit.state = ServiceState::Deactivating;
        }

        try {
            auto restarts_variant = unit_proxy->getProperty("NRestarts")
                                        .onInterface("org.freedesktop.systemd1.Service");
            unit.restart_count = static_cast<uint32_t>(restarts_variant);
        } catch (const sdbus::Error&) {
            unit.restart_count = 0;
        }

        try {
            auto result_variant = unit_proxy->getProperty("Result")
                                      .onInterface("org.freedesktop.systemd1.Service");
            unit.result = static_cast<std::string>(result_variant);
        } catch (const sdbus::Error&) {
        }

        // Short log excerpt for the unit, filtered from the shared persistent
        // journal buffer — no sd_journal_open() per unit per cycle.
        if (journal_) {
            unit.log_excerpt = journal_->excerpt_for_unit(unit_name);
        }
    } catch (const sdbus::Error& err) {
        unit.detail = err.getMessage();
    }

    return unit;
}

Severity ServicesProbe::evaluate_unit_severity(const ServiceUnit& unit) const {
    // Failed state is critical
    if (unit.state == ServiceState::Failed) {
        return Severity::Crit;
    }

    // Check restart count against thresholds
    if (unit.restart_count >= config_.thresholds.service_restart_crit) {
        return Severity::Crit;
    }

    if (unit.restart_count >= config_.thresholds.service_restart_warn) {
        return Severity::Warn;
    }

    // Inactive might be warn depending on context
    if (unit.state == ServiceState::Inactive) {
        return Severity::Warn;
    }

    // Active is OK
    if (unit.state == ServiceState::Active) {
        return Severity::Ok;
    }

    // Unknown states
    return Severity::Unknown;
}

// -----------------------------------------------------------------------------
// TimeSyncProbe
// -----------------------------------------------------------------------------

TimeSyncProbe::TimeSyncProbe(const Config& config, sdbus::IConnection* dbus)
    : config_(config)
    , dbus_(dbus) {}

ProbeResult<TimeSyncStatus> TimeSyncProbe::collect() const {
    TimeSyncStatus status;
    status.source = TimeSyncSource::None;

    if (config_.enable_ntp) {
        status.ntp = collect_ntp();
        if (status.ntp.enabled && status.ntp.state == TimeSyncState::Locked) {
            status.source = TimeSyncSource::Ntp;
        }
    }

    // Reflect PTP config in snapshot even though no PTP probe exists yet.
    // Operators can see enable_ptp=true is registered; startup warns about no probe.
    status.ptp.enabled = config_.enable_ptp;

    if (config_.enable_rtc) {
        status.rtc = collect_rtc();
    }

    status.overall = evaluate_sync_severity(status);

    return status;
}

RtcStatus TimeSyncProbe::collect_rtc() const {
    RtcStatus rtc;
    rtc.enabled = true;

    const auto base = config_.rtc_device;

    // hctosys: did the kernel set the system clock from this RTC at boot?
    {
        std::ifstream f(base / "hctosys");
        int val = 0;
        if (f >> val) {
            rtc.hctosys = (val == 1);
        }
    }

    // battery_voltage in µV → convert to mV
    {
        std::ifstream f(base / "battery_voltage");
        uint64_t uv = 0;
        if (f >> uv) {
            rtc.voltage_mv = static_cast<uint32_t>(uv / 1000);
        }
    }

    // drift: compare RTC epoch time against system clock
    {
        std::ifstream f(base / "since_epoch");
        uint64_t rtc_epoch = 0;
        if (f >> rtc_epoch) {
            auto sys_epoch = static_cast<uint64_t>(
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
            rtc.drift_sec = static_cast<int64_t>(sys_epoch) - static_cast<int64_t>(rtc_epoch);
        }
    }

    return rtc;
}

NtpStatus TimeSyncProbe::collect_ntp() const {
    NtpStatus ntp;
    if (!dbus_) {
        return ntp;
    }
    try {
        auto proxy = sdbus::createProxy(*dbus_,
                                        sdbus::ServiceName("org.freedesktop.timedate1"),
                                        sdbus::ObjectPath("/org/freedesktop/timedate1"));

        auto ntp_enabled_variant =
            proxy->getProperty("NTP").onInterface("org.freedesktop.timedate1");
        auto ntp_synced_variant =
            proxy->getProperty("NTPSynchronized").onInterface("org.freedesktop.timedate1");

        bool ntp_enabled = static_cast<bool>(ntp_enabled_variant);
        bool ntp_synced = static_cast<bool>(ntp_synced_variant);

        ntp.enabled = ntp_enabled;

        if (ntp_synced) {
            ntp.state = TimeSyncState::Locked;
            ntp.last_sync_at = std::chrono::system_clock::now(); // Approximate
        } else if (ntp.enabled) {
            ntp.state = TimeSyncState::FreeRunning;
        }
    } catch (const sdbus::Error&) {
        // On error: return empty NtpStatus so severity degrades.
        // Daemon-level scheduling will retry on next due cycle.
    }

    return ntp;
}

Severity TimeSyncProbe::evaluate_sync_severity(const TimeSyncStatus& status) const {
    Severity sev = Severity::Ok;

    // NTP sync path
    if (config_.enable_ntp) {
        if (status.source == TimeSyncSource::None) {
            sev = Severity::Warn;
        } else if (status.source == TimeSyncSource::Ntp &&
                   status.ntp.state != TimeSyncState::Locked) {
            sev = Severity::Warn;
        }
    }

    // RTC battery voltage — critical backup risk if low
    if (config_.enable_rtc && status.rtc.voltage_mv) {
        const auto& t = config_.thresholds;
        if (*status.rtc.voltage_mv < t.rtc_voltage_crit_mv) {
            sev = Severity::Crit;
        } else if (*status.rtc.voltage_mv < t.rtc_voltage_warn_mv &&
                   sev == Severity::Ok) {
            sev = Severity::Warn;
        }
    }

    return sev;
}

// -----------------------------------------------------------------------------
// UpdateProbe — RAUC D-Bus integration
// -----------------------------------------------------------------------------

bool UpdateProbe::collect_rauc_update(UpdateStatus& status) const {
    // RAUC D-Bus: de.pengutronix.rauc / de.pengutronix.rauc.Installer
    // GetSlotStatus returns a(sa{sv}): array of (slot_name, property_dict)
    using SlotProps = std::map<std::string, sdbus::Variant>;
    using SlotEntry = sdbus::Struct<std::string, SlotProps>;

    if (!dbus_) {
        return false;
    }

    try {
        auto proxy = sdbus::createProxy(*dbus_,
                                        sdbus::ServiceName("de.pengutronix.rauc"),
                                        sdbus::ObjectPath("/"));

        std::vector<SlotEntry> slots;
        proxy->callMethod("GetSlotStatus")
            .onInterface("de.pengutronix.rauc.Installer")
            .storeResultsTo(slots);

        // Helper: safely extract a string property from a slot's property dict
        auto prop_str = [](const SlotProps& props, std::string_view key) -> std::string {
            auto it = props.find(std::string(key));
            if (it == props.end()) return {};
            try { return it->second.get<std::string>(); } catch (...) { return {}; }
        };

        for (const auto& slot : slots) {
            const auto& props = std::get<1>(slot);

            if (prop_str(props, "state") != "booted") {
                continue;
            }

            // Active slot — use bootname (A/B) as the human-readable slot id
            auto bootname = prop_str(props, "bootname");
            if (!bootname.empty()) {
                status.active_slot = bootname;
            }

            LastUpdate update;

            // Bundle identity: prefer "bundle.version/bundle.build"
            auto version = prop_str(props, "bundle.version");
            auto build   = prop_str(props, "bundle.build");
            if (!version.empty() && !build.empty()) {
                update.id = version + "/" + build;
            } else if (!build.empty()) {
                update.id = build;
            } else if (!version.empty()) {
                update.id = version;
            }

            // Parse activated timestamp (ISO 8601: "2026-03-03T09:19:39Z")
            auto ts = prop_str(props, "activated.timestamp");
            if (!ts.empty()) {
                struct tm tm{};
                if (strptime(ts.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm) != nullptr) {
                    auto t = timegm(&tm);
                    if (t != static_cast<time_t>(-1)) {
                        update.installed_at = std::chrono::system_clock::from_time_t(t);
                    }
                }
            }

            // Determine result from boot-status and slot status
            auto boot_status = prop_str(props, "boot-status");
            auto slot_status = prop_str(props, "status");
            if (boot_status == "good" && slot_status == "ok") {
                update.result = UpdateResult::Success;
                auto compatible = prop_str(props, "bundle.compatible");
                if (!compatible.empty()) {
                    update.detail = compatible;
                }
            } else {
                update.result = UpdateResult::Failed;
                update.detail = "boot-status=" + boot_status + " status=" + slot_status;
                status.overall = Severity::Warn;
            }

            status.last_update = std::move(update);
            return true;
        }
    } catch (const sdbus::Error&) {
        // RAUC not available on this system — caller falls back to file-based detection
    }

    return false;
}

} // namespace edge
