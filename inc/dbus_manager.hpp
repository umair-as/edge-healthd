// SPDX-License-Identifier: MIT
// edge-healthd: D-Bus service manager
// Exposes edge.health.Manager on the system bus as a provider.

#pragma once

#include "generated/HealthManagerAdaptor.hpp"
#include "types.hpp"

#include <functional>
#include <mutex>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

namespace edge {

// -----------------------------------------------------------------------------
// HealthManager
//
// Registers the edge.health.Manager D-Bus interface on the system bus.
// Bus name  : edge.health
// Object path: /edge/health/manager
//
// Thread safety: update_severity() may be called from the collection thread;
// OverallSeverity() and TriggerSnapshot() are called from the sdbus event
// loop thread.  A mutex guards severity_str_.
// -----------------------------------------------------------------------------

class HealthManager final
    : public sdbus::AdaptorInterfaces<edge::health::Manager_adaptor>
{
public:
    /// @param connection  System bus connection that owns the "edge.health" name.
    /// @param on_trigger  Callback invoked by TriggerSnapshot() to wake the
    ///                    daemon's collection loop (must be thread-safe).
    HealthManager(sdbus::IConnection& connection,
                  std::function<void()> on_trigger);

    ~HealthManager();

    /// Called by SnapshotDaemon after every collection cycle.
    /// Emits PropertiesChanged when severity changes, and HealthAlarm on
    /// severity degradation (ok→warn, *→crit).
    void update_severity(Severity new_sev, Severity prev_sev);

private:
    // --- Manager_adaptor pure-virtual implementations ---

    /// Returns current overall severity string ("ok"|"warn"|"crit"|"unknown").
    std::string OverallSeverity() override;

    /// Triggers an immediate snapshot collection; returns true on success.
    bool TriggerSnapshot() override;

    // --- Internal state ---
    std::function<void()> on_trigger_;
    mutable std::mutex    mutex_;
    std::string           severity_str_{"unknown"};
    Severity              last_sev_{Severity::Unknown};
};

} // namespace edge
