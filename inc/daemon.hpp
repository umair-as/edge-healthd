// SPDX-License-Identifier: MIT
// edge-healthd: Main daemon
// Orchestrates probes, aggregator, and writer on a periodic schedule

#pragma once

#include "aggregator.hpp"
#include "dbus_manager.hpp"
#include "probes.hpp"
#include "config.hpp"
#include "log.hpp"
#include "writer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <sdbus-c++/sdbus-c++.h>

namespace edge {

// -----------------------------------------------------------------------------
// ProbeSchedule
//
// Controls when a probe's collect() is called.
//
//   interval == 0s  →  collect exactly once at startup, never again
//   interval  > 0s  →  collect every `interval` seconds
//
// On success: next_run advances by interval; has_result = true.
// On failure: next_run is not updated so the probe is retried next cycle.
// -----------------------------------------------------------------------------

struct ProbeSchedule {
    std::chrono::seconds interval{0};               // 0 = collect-once sentinel
    std::chrono::steady_clock::time_point next_run{};
    bool has_result = false;
};

// -----------------------------------------------------------------------------
// SnapshotDaemon
//
// The main service class that:
// - Initializes all probes
// - Runs collection on a periodic schedule
// - Aggregates results and writes snapshot state
// - Handles signals for graceful shutdown
// - Integrates with systemd (notify, watchdog)
// -----------------------------------------------------------------------------

class SnapshotDaemon {
public:
    /// Construct daemon with configuration
    explicit SnapshotDaemon(Config config);

    // Non-copyable, non-movable (owns system resources)
    SnapshotDaemon(const SnapshotDaemon&) = delete;
    SnapshotDaemon& operator=(const SnapshotDaemon&) = delete;
    SnapshotDaemon(SnapshotDaemon&&) = delete;
    SnapshotDaemon& operator=(SnapshotDaemon&&) = delete;

    ~SnapshotDaemon();

    /// Initialize daemon (create directories, verify permissions)
    /// Returns error message on failure, nullopt on success
    [[nodiscard]] std::optional<std::string> initialize();

    /// Run the daemon (blocks until shutdown signal)
    /// Returns exit code
    [[nodiscard]] int run();

    /// Request graceful shutdown
    void request_shutdown();

    /// Trigger immediate snapshot collection (used by signal handlers, tests)
    void collect_now();

    /// Get current snapshot state (thread-safe read)
    [[nodiscard]] SnapshotState current_state() const;

    /// Check if daemon is running
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

private:
    Config config_;

    // Components
    std::unique_ptr<DeviceProbe> device_probe_;
    std::unique_ptr<BootProbe> boot_probe_;
    std::unique_ptr<ServicesProbe> services_probe_;
    std::unique_ptr<ResourcesProbe> resources_probe_;
    std::unique_ptr<TimeSyncProbe> time_sync_probe_;
    std::unique_ptr<UpdateProbe> update_probe_;
    std::unique_ptr<JournalProbe> journal_probe_;
    std::unique_ptr<CrashProbe> crash_probe_;
    std::unique_ptr<SnapshotAggregator> aggregator_;
    std::unique_ptr<SnapshotWriter> writer_;
    std::unique_ptr<NetlinkMonitor> nl_monitor_;  // Netlink monitor instance

    // D-Bus service (optional — gracefully absent if bus unavailable)
    // Declared before health_manager_ so connection outlives the adaptor.
    std::unique_ptr<sdbus::IConnection> dbus_connection_;
    std::unique_ptr<HealthManager>      health_manager_;
    // RAUC signal subscription — proxy on the shared dbus_connection_, kept alive
    // so the handler stays registered. Null when D-Bus is unavailable or
    // update tracking is disabled.
    std::unique_ptr<sdbus::IProxy>      rauc_proxy_;
    // Set by the RAUC Completed signal handler; cleared after UpdateProbe runs.
    // Causes collection_cycle() to force UpdateProbe due regardless of schedule.
    std::atomic<bool>                   rauc_update_pending_{false};

    // Per-probe collection schedules — set in initialize(), used in collection_cycle()
    ProbeSchedule device_schedule_;
    ProbeSchedule boot_schedule_;
    ProbeSchedule services_schedule_;
    ProbeSchedule resources_schedule_;
    ProbeSchedule time_sync_schedule_;
    ProbeSchedule update_schedule_;
    ProbeSchedule journal_schedule_;
    ProbeSchedule crash_schedule_;

    // Last known good results — retained across cycles when a probe is not due
    // or when a probe fails. Aggregated into every snapshot.
    SnapshotState last_known_good_;

    // Monotonic cycle counter — incremented every collection_cycle(), serialized
    // into SnapshotState::cycle so operators can confirm cadence without relying
    // on the rate-limited "Snapshot collected" log entry.
    uint64_t cycle_count_{0};

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> trigger_requested_{false};
    mutable std::mutex state_mutex_;
    SnapshotState current_state_;
    Severity last_severity_{Severity::Unknown};

    // Tracks the most recently alarmed crash fingerprint. When the current
    // crash fingerprint changes (new artifact set captured), emit a
    // distinguishable HealthAlarm with component="crash" so downstream
    // consumers can route on it independently of overall severity.
    std::optional<std::string> last_alarmed_crash_fp_;

    // eventfd used to interrupt the collection wait on demand. Written by the
    // async-signal-safe SIGTERM/SIGINT handler and by the D-Bus TriggerSnapshot
    // / RAUC callbacks, so a shutdown or trigger breaks poll() within ~1s
    // instead of waiting out collect_interval. -1 if eventfd creation failed.
    int wakeup_fd_{-1};
    // Guards last_trigger_time_ for the TriggerSnapshot rate-limit check.
    std::mutex              cv_mutex_;
    std::chrono::steady_clock::time_point last_trigger_time_{
        std::chrono::steady_clock::time_point::min()};

    // Internal methods
    void collection_cycle();
    void setup_signal_handlers();
    // Best-effort write to wakeup_fd_ to break the main-loop poll() early.
    void wake() noexcept;
    void notify_systemd_ready();
    void start_watchdog_thread();
    void stop_watchdog_thread();
    void update_watchdog_heartbeat() noexcept;
    void watchdog_loop(std::stop_token st);

    std::jthread watchdog_thread_;
    // Notified by the jthread stop-callback so the watchdog's interruptible
    // sleep returns immediately on request_stop() (fast join on shutdown).
    std::condition_variable_any watchdog_cv_;
    std::atomic<int64_t> watchdog_heartbeat_us_{0};
    std::chrono::microseconds watchdog_timeout_{0};
};

namespace detail {

// Block up to `timeout` waiting for `fd` (an eventfd/pipe) to become readable,
// draining it if it fired. Returns true on wakeup (fd signaled, or poll
// interrupted by a signal), false on timeout. Exposed for lifecycle tests.
[[nodiscard]] bool wait_wakeup_fd(int fd, std::chrono::milliseconds timeout);

// Interruptible sleep used by the watchdog loop: block up to `timeout` on `cv`,
// returning true immediately if `st` requests stop, false on timeout. Exposed
// for lifecycle tests.
[[nodiscard]] bool watchdog_sleep(std::condition_variable_any& cv,
                                  std::stop_token st,
                                  std::chrono::nanoseconds timeout);

} // namespace detail

// -----------------------------------------------------------------------------
// Systemd integration helpers
// -----------------------------------------------------------------------------

namespace systemd {

/// Check if running under systemd
[[nodiscard]] bool is_systemd_managed();

/// Notify systemd that service is ready; sets STATUS=v<version> in sd_notify
/// so the version is visible in `systemctl status` without journalctl.
void notify_ready(std::string_view version);

/// Notify systemd watchdog (call periodically)
void notify_watchdog();

/// Notify systemd of stopping
void notify_stopping();

/// Update the STATUS field shown in `systemctl status` with live severity.
void notify_status(std::string_view version, std::string_view severity);

/// Get watchdog timeout (0 if not configured)
[[nodiscard]] std::chrono::microseconds watchdog_timeout();

} // namespace systemd

} // namespace edge
