// SPDX-License-Identifier: MIT
// edge-healthd: Daemon implementation

#include "daemon.hpp"
#include "netlink_monitor.hpp"
#include "version.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <thread>

#include <sdbus-c++/sdbus-c++.h>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

namespace edge {

namespace {
volatile std::sig_atomic_t g_shutdown_signal = 0;

void handle_shutdown_signal(int) {
    g_shutdown_signal = 1;
}

int64_t monotonic_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}
} // namespace

// -----------------------------------------------------------------------------
// SnapshotDaemon implementation
// -----------------------------------------------------------------------------

SnapshotDaemon::SnapshotDaemon(Config config)
    : config_(std::move(config)) {}

SnapshotDaemon::~SnapshotDaemon() {
    stop_watchdog_thread();
    if (running_.load()) {
        request_shutdown();
    }
}

std::optional<std::string> SnapshotDaemon::initialize() {
    // Create state directory if needed
    std::error_code ec;
    std::filesystem::create_directories(config_.state_dir, ec);
    if (ec) {
        return "Failed to create state directory: " + ec.message();
    }

    // Warn about unimplemented PTP probe
    if (config_.enable_ptp) {
        log::warn("enable_ptp=true in config but PTP probe is not yet implemented; ptp.enabled will reflect config, offset thresholds unused");
    }

    // Initialize NetlinkMonitor first
    nl_monitor_ = std::make_unique<NetlinkMonitor>();
    if (!nl_monitor_->init()) {
        return "Failed to initialize NetlinkMonitor";
    }

    // Initialize D-Bus before probes so the shared connection can be passed in.
    // All D-Bus-capable probes (Services, TimeSync, Update) use this single
    // connection for outbound calls. The connection also backs the health manager
    // service and the RAUC signal subscription — one fd, one event loop thread.
    try {
        dbus_connection_ = sdbus::createSystemBusConnection(
            sdbus::ServiceName{"edge.health"});
        dbus_connection_->setMethodCallTimeout(config_.dbus_timeout);

        health_manager_ = std::make_unique<HealthManager>(
            *dbus_connection_,
            [this]() -> bool {
                const auto now = std::chrono::steady_clock::now();
                {
                    std::lock_guard lock(cv_mutex_);
                    // Enforce minimum interval between triggered collections.
                    // Protects against unbounded wakeup floods from local D-Bus
                    // clients driving continuous collection cycles (flash wear +
                    // CPU burn). Set trigger_min_interval_sec=0 to disable.
                    if (config_.trigger_min_interval.count() > 0 &&
                        last_trigger_time_ != std::chrono::steady_clock::time_point::min() &&
                        now - last_trigger_time_ < config_.trigger_min_interval) {
                        log::debug("TriggerSnapshot rate-limited (min interval: " +
                                   std::to_string(config_.trigger_min_interval.count()) + "s)");
                        return false;
                    }
                    last_trigger_time_ = now;
                    trigger_requested_.store(true);
                }
                cv_.notify_one();
                return true;
            });
    } catch (const sdbus::Error& e) {
        log::warn("D-Bus service unavailable, continuing without it: " +
                  std::string(e.getMessage()));
        dbus_connection_.reset();
        health_manager_.reset();
    }

    // Subscribe to RAUC Completed signal on the shared connection so UpdateProbe
    // runs immediately after an OTA install. Independent try/catch — RAUC may not
    // be installed. If D-Bus is unavailable the subscription is silently skipped
    // (dbus_connection_ is null) and the 1800s poll remains the only mechanism.
    if (config_.enable_update_tracking && dbus_connection_) {
        try {
            rauc_proxy_ = sdbus::createProxy(
                *dbus_connection_,
                sdbus::ServiceName{"de.pengutronix.rauc"},
                sdbus::ObjectPath{"/"}
            );

            rauc_proxy_
                ->uponSignal(sdbus::SignalName{"Completed"})
                .onInterface(sdbus::InterfaceName{"de.pengutronix.rauc.Installer"})
                .call([this](int32_t result) {
                    log::info("RAUC Completed signal received (result=" +
                              std::to_string(result) + "); scheduling immediate update check");
                    rauc_update_pending_.store(true, std::memory_order_relaxed);
                    {
                        std::lock_guard lock(cv_mutex_);
                        trigger_requested_.store(true);
                    }
                    cv_.notify_one();
                });

            log::info("Subscribed to RAUC de.pengutronix.rauc.Installer.Completed signal");
        } catch (const sdbus::Error& e) {
            log::warn("RAUC signal subscription failed (RAUC not installed?): " +
                      std::string(e.getMessage()));
            rauc_proxy_.reset();
        }
    }

    // Initialize probes — D-Bus-capable probes receive the shared connection.
    // Null is safe: each probe degrades gracefully when dbus_ is nullptr.
    sdbus::IConnection* dbus = dbus_connection_.get();
    device_probe_ = std::make_unique<DeviceProbe>(config_);
    boot_probe_ = std::make_unique<BootProbe>(config_, config_.state_dir);
    services_probe_ = std::make_unique<ServicesProbe>(
        config_, dbus, config_.monitored_services);
    resources_probe_ = std::make_unique<ResourcesProbe>(
        config_,
        *nl_monitor_,
        std::span<const std::string>(config_.monitored_mounts),
        std::span<const std::string>(config_.monitored_interfaces)
    );
    time_sync_probe_ = std::make_unique<TimeSyncProbe>(config_, dbus);
    update_probe_ = std::make_unique<UpdateProbe>(config_, dbus);
    journal_probe_ = std::make_unique<JournalProbe>(config_);
    crash_probe_ = std::make_unique<CrashProbe>(config_, config_.state_dir);

    // Set up per-probe collection schedules.
    // interval=0 means collect-once at startup (data is runtime-immutable).
    device_schedule_    = { std::chrono::seconds{0} };
    boot_schedule_      = { config_.collect_interval };
    services_schedule_  = { config_.collect_interval };
    resources_schedule_ = { config_.collect_interval };
    time_sync_schedule_ = { config_.time_sync_interval };
    update_schedule_    = { config_.update_check_interval };
    journal_schedule_   = { config_.collect_interval };
    crash_schedule_     = { config_.collect_interval };

    // Initialize aggregator and writer
    aggregator_ = std::make_unique<SnapshotAggregator>(config_);
    writer_ = std::make_unique<SnapshotWriter>(config_.snapshot_file);

    return std::nullopt;
}

int SnapshotDaemon::run() {
    running_.store(true);
    setup_signal_handlers();

    log::daemon_starting(EDGE_HEALTHD_VERSION);

    // Initial collection
    collection_cycle();

    // Notify systemd we're ready
    notify_systemd_ready();
    log::daemon_ready();

    // Mark boot as successful after READY + first collection
    boot_probe_->mark_boot_success();
    start_watchdog_thread();

    // Start D-Bus event loop in its own internal thread
    if (dbus_connection_) {
        dbus_connection_->enterEventLoopAsync();
    }

    // Main loop — wait for interval OR a TriggerSnapshot/shutdown wakeup
    while (!shutdown_requested_.load()) {
        if (g_shutdown_signal != 0) {
            request_shutdown();
            break;
        }

        {
            std::unique_lock lock(cv_mutex_);
            cv_.wait_for(lock, config_.collect_interval, [this] {
                return shutdown_requested_.load() || trigger_requested_.load();
            });
            trigger_requested_.store(false);
        }

        if (shutdown_requested_.load() || g_shutdown_signal != 0) {
            request_shutdown();
            break;
        }

        collection_cycle();
    }

    stop_watchdog_thread();
    running_.store(false);
    log::daemon_stopping();
    systemd::notify_stopping();

    return 0;
}

void SnapshotDaemon::request_shutdown() {
    shutdown_requested_.store(true);
    cv_.notify_all();
    if (dbus_connection_) {
        dbus_connection_->leaveEventLoop();
    }
}

void SnapshotDaemon::collect_now() {
    collection_cycle();
}

SnapshotState SnapshotDaemon::current_state() const {
    std::lock_guard lock(state_mutex_);
    return current_state_;
}

void SnapshotDaemon::collection_cycle() {
    update_watchdog_heartbeat();

    auto now = std::chrono::steady_clock::now();

    // Drain pending netlink events before resource collection
    if (nl_monitor_) {
        nl_monitor_->drain_events();
    }

    // Collect probe if due; on success update last_known_good_ and advance schedule.
    // On failure retain last known good value — transient errors don't blank the snapshot.
    // interval=0 probes (DeviceProbe) collect exactly once: is_due() returns false once
    // has_result=true, regardless of next_run.
    auto maybe_collect = [&]<typename P, typename T>(
        P& probe, ProbeSchedule& sched, T& cache, std::string_view name) {
        const bool due = !sched.has_result ||
                         (sched.interval.count() > 0 && now >= sched.next_run);
        if (!due) return;

        auto result = probe.collect();
        if (result) {
            cache = std::move(*result);
            sched.next_run = now + sched.interval;
            sched.has_result = true;
        } else {
            log::probe_error(std::string(name), result.error().message);
            // has_result stays false on first-run failure → retried next cycle
        }
    };

    maybe_collect(*device_probe_,    device_schedule_,    last_known_good_.device,    "device");
    maybe_collect(*boot_probe_,      boot_schedule_,      last_known_good_.boot,      "boot");
    maybe_collect(*services_probe_,  services_schedule_,  last_known_good_.services,  "services");
    maybe_collect(*resources_probe_, resources_schedule_, last_known_good_.resources, "resources");
    maybe_collect(*time_sync_probe_, time_sync_schedule_, last_known_good_.time_sync, "time_sync");

    // If RAUC fired a Completed signal since the last cycle, make UpdateProbe due
    // immediately regardless of its regular schedule. Clear the flag before collect
    // so a second signal that arrives during collect is not lost.
    if (rauc_update_pending_.exchange(false, std::memory_order_relaxed)) {
        update_schedule_.next_run = now;
    }
    maybe_collect(*update_probe_,    update_schedule_,    last_known_good_.update,    "update");
    maybe_collect(*journal_probe_,   journal_schedule_,   last_known_good_.journal,   "journal");
    maybe_collect(*crash_probe_,     crash_schedule_,     last_known_good_.crash,     "crash");

    // Aggregate using last known good values for all probes
    auto state = aggregator_->aggregate(
        last_known_good_.device,
        last_known_good_.boot,
        last_known_good_.services,
        last_known_good_.resources,
        last_known_good_.time_sync,
        last_known_good_.update,
        last_known_good_.journal,
        last_known_good_.crash
    );

    state.cycle = ++cycle_count_;

    // Write to file
    if (auto result = writer_->write(state); !result) {
        log::writer_error("Failed to write snapshot: " + result.error().message);
    }

    // Update current state and capture severity transition
    Severity prev_sev = last_severity_;
    {
        std::lock_guard lock(state_mutex_);
        current_state_ = std::move(state);
        last_severity_ = current_state_.summary.severity;
    }
    const Severity new_sev = last_severity_;

    // Push severity and cached logs to D-Bus manager.
    if (health_manager_) {
        health_manager_->update_severity(new_sev, prev_sev);
        health_manager_->update_recent_logs(current_state_.journal.recent_errors);

        // Distinguishable crash alarm: fire when an unacknowledged crash with a
        // new fingerprint appears. Reset the latch when the artifact set is
        // cleared (pstore drained externally) so a future crash re-alarms.
        const auto& crash = current_state_.crash;
        if (crash.present && !crash.acknowledged && crash.fingerprint) {
            if (last_alarmed_crash_fp_ != crash.fingerprint) {
                std::string msg = "kernel panic detected: " +
                                  std::to_string(crash.artifact_count) +
                                  " pstore artifact(s), fingerprint=" +
                                  *crash.fingerprint;
                health_manager_->emit_alarm("crash", msg, Severity::Crit);
                last_alarmed_crash_fp_ = crash.fingerprint;
            }
        } else if (!crash.present) {
            last_alarmed_crash_fp_.reset();
        }
    }

    // Log snapshot with severity
    log::snapshot_collected(to_string(new_sev));

    // Keep systemctl status current: version + live severity visible without journalctl
    systemd::notify_status(EDGE_HEALTHD_VERSION, to_string(new_sev));

    update_watchdog_heartbeat();
}

void SnapshotDaemon::setup_signal_handlers() {
    std::signal(SIGTERM, handle_shutdown_signal);
    std::signal(SIGINT, handle_shutdown_signal);
}

void SnapshotDaemon::notify_systemd_ready() {
    systemd::notify_ready(EDGE_HEALTHD_VERSION);
}

void SnapshotDaemon::start_watchdog_thread() {
    if (!systemd::is_systemd_managed()) {
        return;
    }

    auto timeout = systemd::watchdog_timeout();
    if (timeout.count() <= 0) {
        return;
    }

    watchdog_timeout_ = timeout;

    auto collect_interval_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            config_.collect_interval);
    if (collect_interval_us >= (timeout / 2)) {
        log::warn("collect_interval >= watchdog timeout / 2; systemd may restart the service");
    }

    if (watchdog_thread_.joinable()) {
        return;  // Already running
    }

    update_watchdog_heartbeat();
    watchdog_thread_ = std::jthread([this](std::stop_token st) {
        watchdog_loop(std::move(st));
    });
}

void SnapshotDaemon::stop_watchdog_thread() {
    watchdog_thread_.request_stop();
    // std::jthread destructor auto-joins
}

void SnapshotDaemon::update_watchdog_heartbeat() noexcept {
    watchdog_heartbeat_us_.store(monotonic_us(), std::memory_order_relaxed);
}

void SnapshotDaemon::watchdog_loop(std::stop_token st) {
    auto timeout = watchdog_timeout_;
    auto interval = timeout / 2;
    if (interval.count() <= 0) {
        interval = std::chrono::microseconds(1);
    }

    while (!st.stop_requested()) {
        std::this_thread::sleep_for(interval);

        auto last_us = watchdog_heartbeat_us_.load(std::memory_order_relaxed);
        auto age_us = monotonic_us() - last_us;
        if (age_us <= timeout.count()) {
            systemd::notify_watchdog();
        }
    }
}

// -----------------------------------------------------------------------------
// Systemd helpers
// -----------------------------------------------------------------------------

namespace systemd {

bool is_systemd_managed() {
#ifdef EDGE_HAS_SYSTEMD
    return sd_booted() > 0;
#else
    return false;
#endif
}

void notify_ready(std::string_view version) {
#ifdef EDGE_HAS_SYSTEMD
    // READY=1 only — collection_cycle() already called notify_status() with the
    // actual first-run severity. Appending STATUS here would overwrite that with
    // "starting" and leave systemctl showing a stale string until the next cycle.
    (void)version;
    sd_notify(0, "READY=1");
#else
    (void)version;
#endif
}

void notify_status(std::string_view version, std::string_view severity) {
#ifdef EDGE_HAS_SYSTEMD
    sd_notifyf(0, "STATUS=v%.*s — %.*s",
               static_cast<int>(version.size()), version.data(),
               static_cast<int>(severity.size()), severity.data());
#else
    (void)version;
    (void)severity;
#endif
}

void notify_watchdog() {
#ifdef EDGE_HAS_SYSTEMD
    sd_notify(0, "WATCHDOG=1");
#endif
}

void notify_stopping() {
#ifdef EDGE_HAS_SYSTEMD
    sd_notify(0, "STOPPING=1");
#endif
}

std::chrono::microseconds watchdog_timeout() {
#ifdef EDGE_HAS_SYSTEMD
    uint64_t usec = 0;
    if (sd_watchdog_enabled(0, &usec) > 0) {
        return std::chrono::microseconds(usec);
    }
#endif
    return std::chrono::microseconds(0);
}

} // namespace systemd

} // namespace edge
