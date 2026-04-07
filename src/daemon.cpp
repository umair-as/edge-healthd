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
        log::warn("enable_ptp=true in config but PTP probe is not yet implemented; ignoring");
    }

    // Initialize NetlinkMonitor first
    nl_monitor_ = std::make_unique<NetlinkMonitor>();
    if (!nl_monitor_->init()) {
        return "Failed to initialize NetlinkMonitor";
    }

    // Initialize probes
    device_probe_ = std::make_unique<DeviceProbe>(config_);
    boot_probe_ = std::make_unique<BootProbe>(config_, config_.state_dir);
    services_probe_ = std::make_unique<ServicesProbe>(
        config_, config_.monitored_services);
    
    // FIX: Pass NetlinkMonitor instance to ResourcesProbe
    resources_probe_ = std::make_unique<ResourcesProbe>(
        config_, 
        *nl_monitor_,  // Pass NetlinkMonitor instance
        std::span<const std::string>(config_.monitored_mounts), 
        std::span<const std::string>(config_.monitored_interfaces)
    );
    
    time_sync_probe_ = std::make_unique<TimeSyncProbe>(config_);
    update_probe_ = std::make_unique<UpdateProbe>(config_);
    journal_probe_ = std::make_unique<JournalProbe>(config_);

    // Initialize aggregator and writer
    aggregator_ = std::make_unique<SnapshotAggregator>(config_);
    writer_ = std::make_unique<SnapshotWriter>(config_.snapshot_file);

    // Initialize D-Bus service (optional — daemon runs without it if bus is unavailable)
    try {
        dbus_connection_ = sdbus::createSystemBusConnection(
            sdbus::ServiceName{"edge.health"});

        health_manager_ = std::make_unique<HealthManager>(
            *dbus_connection_,
            [this]() {
                {
                    std::lock_guard lock(cv_mutex_);
                    trigger_requested_.store(true);
                }
                cv_.notify_one();
            });
    } catch (const sdbus::Error& e) {
        log::warn("D-Bus service unavailable, continuing without it: " +
                  std::string(e.getMessage()));
        dbus_connection_.reset();
        health_manager_.reset();
    }

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

    // Drain pending netlink events to refresh the cache before collection
    if (nl_monitor_) {
        nl_monitor_->drain_events();
    }

    // Collect from all probes
    auto device_result = device_probe_->collect();
    auto boot_result = boot_probe_->collect();
    auto services_result = services_probe_->collect();
    auto resources_result = resources_probe_->collect();
    auto time_sync_result = time_sync_probe_->collect();
    auto update_result = update_probe_->collect();
    auto journal_result = journal_probe_->collect();

    // Log any collection errors
    if (!device_result) {
        log::probe_error("device", device_result.error().message);
    }
    if (!boot_result) {
        log::probe_error("boot", boot_result.error().message);
    }
    if (!services_result) {
        log::probe_error("services", services_result.error().message);
    }
    if (!resources_result) {
        log::probe_error("resources", resources_result.error().message);
    }
    if (!time_sync_result) {
        log::probe_error("time_sync", time_sync_result.error().message);
    }
    if (!update_result) {
        log::probe_error("update", update_result.error().message);
    }
    if (!journal_result) {
        log::probe_error("journal", journal_result.error().message);
    }

    // Aggregate (use partial aggregation to handle failures)
    auto state = aggregator_->aggregate_partial(
        device_result ? std::optional(*device_result) : std::nullopt,
        boot_result ? std::optional(*boot_result) : std::nullopt,
        services_result ? std::optional(*services_result) : std::nullopt,
        resources_result ? std::optional(*resources_result) : std::nullopt,
        time_sync_result ? std::optional(*time_sync_result) : std::nullopt,
        update_result ? std::optional(*update_result) : std::nullopt,
        journal_result ? std::optional(*journal_result) : std::nullopt
    );

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
    // STATUS persists in systemctl status output — surfacing version avoids
    // needing journalctl to identify what's running on the target.
    sd_notifyf(0, "READY=1\nSTATUS=v%.*s — starting", static_cast<int>(version.size()), version.data());
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
