// SPDX-License-Identifier: MIT
// edge-healthd: D-Bus service manager
// Exposes edge.health.Manager on the system bus as a provider.

#pragma once

#include "generated/HealthManagerAdaptor.hpp"
#include "types.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

namespace edge {

// -----------------------------------------------------------------------------
// HealthManager
//
// Registers the edge.health.Manager D-Bus interface on the system bus.
// Bus name  : edge.health
// Object path: /edge/health/manager
//
// Thread safety: update_severity() and update_recent_logs() may be called from
// the collection thread; all D-Bus callbacks run on the sdbus event-loop thread.
// A single mutex guards all mutable state.
// -----------------------------------------------------------------------------

class HealthManager final
    : public sdbus::AdaptorInterfaces<edge::health::Manager_adaptor>
{
public:
    /// @param connection  System bus connection that owns the "edge.health" name.
    /// @param on_trigger  Callback invoked by TriggerSnapshot() to wake the
    ///                    daemon's collection loop (must be thread-safe).
    /// @param on_acknowledge  Callback invoked by AcknowledgeCrash() with the
    ///                    caller-supplied fingerprint; returns true only if it
    ///                    matches the daemon's current panic fingerprint and the
    ///                    acknowledgement was persisted (must be thread-safe).
    HealthManager(sdbus::IConnection& connection,
                  std::function<bool()> on_trigger,
                  std::function<bool(const std::string&)> on_acknowledge = {});

    ~HealthManager();

    /// Called by SnapshotDaemon after every collection cycle.
    /// Emits PropertiesChanged when severity changes, and HealthAlarm on
    /// severity degradation (ok→warn, *→crit).
    void update_severity(Severity new_sev, Severity prev_sev);

    /// Called by SnapshotDaemon after every collection cycle to refresh the
    /// in-memory log cache served by GetRecentLogs(). No journal I/O.
    void update_recent_logs(std::vector<std::string> logs);

    /// Emit a HealthAlarm with an explicit component name (e.g. "crash").
    /// Used by SnapshotDaemon when a probe wants to surface a distinguishable
    /// alarm beyond the overall-severity transition path.
    void emit_alarm(const std::string& component,
                    const std::string& message,
                    Severity severity);

private:
    // --- Manager_adaptor pure-virtual implementations ---

    /// Returns current overall severity string ("ok"|"warn"|"crit"|"unknown").
    std::string OverallSeverity() override;

    /// Triggers an immediate snapshot collection; returns true on success.
    bool TriggerSnapshot() override;

    /// Returns up to max_lines recent journal error strings from the in-memory
    /// cache — no sd_journal_open() call, zero additional syscalls.
    std::vector<std::string> GetRecentLogs(uint32_t max_lines) override;

    /// Acknowledges the current kernel-panic fingerprint. Returns true only if
    /// `fingerprint` matches the daemon's current panic fingerprint and the ack
    /// was persisted; false otherwise (stale/unknown fingerprint, no callback).
    bool AcknowledgeCrash(const std::string& fingerprint) override;

    // --- Internal state ---
    std::function<bool()>    on_trigger_;
    std::function<bool(const std::string&)> on_acknowledge_;
    mutable std::mutex       mutex_;
    std::string              severity_str_{"unknown"};
    Severity                 last_sev_{Severity::Unknown};
    std::vector<std::string> logs_cache_;
};

} // namespace edge
