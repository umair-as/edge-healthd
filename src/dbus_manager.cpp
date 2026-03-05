// SPDX-License-Identifier: MIT
// edge-healthd: D-Bus service manager implementation

#include "dbus_manager.hpp"
#include "log.hpp"

#include <string>

namespace edge {

namespace {

const sdbus::ObjectPath kObjectPath{"/edge/health/manager"};

// Returns true if new_sev represents a degradation relative to prev_sev.
// Degradation order: Unknown < Ok < Warn < Crit.
bool is_degradation(Severity prev_sev, Severity new_sev) {
    auto rank = [](Severity s) -> int {
        switch (s) {
            case Severity::Unknown: return 0;
            case Severity::Ok:      return 1;
            case Severity::Warn:    return 2;
            case Severity::Crit:    return 3;
        }
        return 0;
    };
    return rank(new_sev) > rank(prev_sev);
}

} // namespace

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------

HealthManager::HealthManager(sdbus::IConnection& connection,
                             std::function<void()> on_trigger)
    : AdaptorInterfaces(connection, kObjectPath)
    , on_trigger_(std::move(on_trigger))
{
    registerAdaptor();
    log::info("D-Bus manager registered at edge.health" +
              std::string(kObjectPath));
}

HealthManager::~HealthManager()
{
    unregisterAdaptor();
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void HealthManager::update_severity(Severity new_sev, Severity prev_sev)
{
    const std::string new_str{to_string(new_sev)};
    bool changed = false;

    {
        std::lock_guard lock(mutex_);
        if (severity_str_ != new_str) {
            severity_str_ = new_str;
            changed = true;
        }
        last_sev_ = new_sev;
    }

    if (changed) {
        // Notify D-Bus clients watching the OverallSeverity property.
        getObject().emitPropertiesChangedSignal(
            INTERFACE_NAME,
            {sdbus::PropertyName{"OverallSeverity"}});
    }

    // Emit HealthAlarm on severity degradation transitions.
    if (is_degradation(prev_sev, new_sev)) {
        const std::string msg = std::string(to_string(prev_sev)) +
                                " -> " + new_str;
        emitHealthAlarm("overall", msg, new_str);
        log::info("HealthAlarm emitted: " + msg);
    }
}

// -----------------------------------------------------------------------------
// Manager_adaptor overrides (called from sdbus event-loop thread)
// -----------------------------------------------------------------------------

std::string HealthManager::OverallSeverity()
{
    std::lock_guard lock(mutex_);
    return severity_str_;
}

bool HealthManager::TriggerSnapshot()
{
    if (on_trigger_) {
        on_trigger_();
    }
    return true;
}

} // namespace edge
