// SPDX-License-Identifier: MIT
// edge-healthd: Probes (D-Bus) implementation

#include "probes.hpp"
#include "config.hpp"

#include <cctype>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <sdbus-c++/sdbus-c++.h>

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
                                     std::span<const std::string> monitored_units)
    : config_(config)
    , monitored_units_(monitored_units.begin(), monitored_units.end()) {

    // Use config defaults if not specified
    if (monitored_units_.empty()) {
        monitored_units_ = config_.monitored_services;
    }
}

ProbeResult<ServicesStatus> ServicesProbe::collect() const {
    ServicesStatus status;
    status.overall = Severity::Ok;

    std::unique_ptr<sdbus::IConnection> connection;
    try {
        connection = sdbus::createSystemBusConnection();
        connection->setMethodCallTimeout(config_.dbus_timeout);
    } catch (const sdbus::Error&) {
        for (const auto& unit_name : monitored_units_) {
            ServiceUnit unit;
            unit.name = unit_name;
            unit.state = ServiceState::Unknown;
            unit.severity = Severity::Unknown;
            unit.detail = "D-Bus connection failed";
            status.units.push_back(std::move(unit));
        }
        status.overall = Severity::Unknown;
        return status;
    }

    for (const auto& unit_name : monitored_units_) {
        auto unit = query_unit(unit_name, *connection);
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

TimeSyncProbe::TimeSyncProbe(const Config& config)
    : config_(config) {}

ProbeResult<TimeSyncStatus> TimeSyncProbe::collect() const {
    TimeSyncStatus status;
    status.source = TimeSyncSource::None;

    if (config_.enable_ntp) {
        status.ntp = collect_ntp();
        if (status.ntp.enabled && status.ntp.state == TimeSyncState::Locked) {
            status.source = TimeSyncSource::Ntp;
        }
    }

    status.overall = evaluate_sync_severity(status);

    return status;
}

NtpStatus TimeSyncProbe::collect_ntp() const {
    NtpStatus ntp;
    try {
        auto connection = sdbus::createSystemBusConnection();
        connection->setMethodCallTimeout(config_.dbus_timeout);
        auto proxy = sdbus::createProxy(*connection,
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
        return ntp;
    }

    return ntp;
}

Severity TimeSyncProbe::evaluate_sync_severity(const TimeSyncStatus& status) const {
    // No sync source is warning
    if (status.source == TimeSyncSource::None) {
        if (!config_.enable_ntp) {
            return Severity::Ok; // Sync not configured
        }
        return Severity::Warn;
    }

    // Locked state is OK
    if (status.source == TimeSyncSource::Ntp && status.ntp.state == TimeSyncState::Locked) {
        return Severity::Ok;
    }

    return Severity::Warn;
}

} // namespace edge
