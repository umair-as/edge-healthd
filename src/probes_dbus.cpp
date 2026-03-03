// SPDX-License-Identifier: MIT
// edge-healthd: Probes (D-Bus) implementation

#include "probes.hpp"
#include "config.hpp"
#include "journal.hpp"

#include <cctype>
#include <chrono>
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
 
#ifdef EDGE_HAS_SYSTEMD
// Helper function to collect journal log excerpts using sd_journal
static std::vector<std::string> get_journal_excerpt(const std::string& unit_name, const Config& cfg) {
    std::vector<JournalEntry> raw;
    sd_journal* journal = nullptr;
    int ret = sd_journal_open(&journal, SD_JOURNAL_SYSTEM);
    if (ret < 0 || !journal) {
        // Try current_user as fallback for unprivileged runs
        ret = sd_journal_open(&journal, SD_JOURNAL_CURRENT_USER);
        if (ret < 0 || !journal) {
            // Try local-only as last fallback
            ret = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
            if (ret < 0 || !journal) {
                log::debug("Failed to open journal for unit: " + unit_name);
                return {};
            }
        }
    }

    struct JournalGuard { sd_journal* j; ~JournalGuard(){ if (j) sd_journal_close(j); } } guard{journal};

    std::string unit_filter = "_SYSTEMD_UNIT=" + unit_name;
    ret = sd_journal_add_match(journal, unit_filter.c_str(), 0);
    if (ret < 0) {
        log::debug("Failed to add journal filter for unit: " + unit_name);
        return {};
    }

    // Also try alternate fields that journalctl -u uses (UNIT= and SYSLOG_IDENTIFIER)
    std::string unit_field = "UNIT=" + unit_name;
    sd_journal_add_disjunction(journal);
    sd_journal_add_match(journal, unit_field.c_str(), 0);
    std::string syslog_filter = "SYSLOG_IDENTIFIER=" + unit_name;
    sd_journal_add_disjunction(journal);
    sd_journal_add_match(journal, syslog_filter.c_str(), 0);

    ret = sd_journal_seek_tail(journal);
    if (ret < 0) {
        log::debug("Failed to seek to end of journal");
        return {};
    }

    // Move to most recent entry
    ret = sd_journal_previous(journal);
    if (ret <= 0) {
        return {}; // no entries
    }

    const size_t max_collect = std::max<size_t>(cfg.log_excerpt_max_lines ? cfg.log_excerpt_max_lines : 20, 200);

    for (size_t i = 0; i < max_collect; ++i) {
        // Read timestamp
        uint64_t usec = 0;
        if (sd_journal_get_realtime_usec(journal, &usec) < 0) usec = 0;

        // Read priority
        int pri = 6;
        const void* pdata = nullptr;
        size_t plen = 0;
        if (sd_journal_get_data(journal, "PRIORITY", &pdata, &plen) >= 0 && pdata) {
            const char* pstr = static_cast<const char*>(pdata);
            if (plen > 9 && std::strncmp(pstr, "PRIORITY=", 9) == 0) pstr += 9;
            pri = std::atoi(pstr);
        }

        // Read message
        const void* data = nullptr;
        size_t length = 0;
        std::string msg;
        if (sd_journal_get_data(journal, "MESSAGE", &data, &length) >= 0 && data) {
            const char* ptr = static_cast<const char*>(data);
            if (length > 8 && std::strncmp(ptr, "MESSAGE=", 8) == 0) {
                ptr += 8;
                length -= 8;
            }
            msg.assign(ptr, ptr + std::min(length, static_cast<size_t>(1024)));
        }

        raw.push_back(JournalEntry{usec, pri, std::move(msg)});

        ret = sd_journal_previous(journal);
        if (ret <= 0) break;
    }

    // Use filtering helper to apply priority/window/max_lines and format lines.
    return filter_journal_entries(cfg, raw);
}
#endif

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

        // Collect a short log excerpt for the unit (recent lines) using sd-journal when available.
#ifdef EDGE_HAS_SYSTEMD
        try {
            auto lines = get_journal_excerpt(unit_name, config_);
            if (!lines.empty()) {
                unit.log_excerpt = std::move(lines);
                log::debug("Added " + std::to_string(unit.log_excerpt.size()) + " log lines for " + unit_name);
            } else {
                log::debug("No log lines found for " + unit_name);
            }
        } catch (const std::exception& e) {
            log::debug("Failed to get logs for " + unit_name + ": " + e.what());
        }
#endif
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
