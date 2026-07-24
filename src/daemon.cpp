// SPDX-License-Identifier: MIT
// edge-healthd: Daemon implementation

#include "daemon.hpp"
#include "atomic_file.hpp"
#include "journal_reader.hpp"
#include "netlink_monitor.hpp"
#include "version.hpp"

#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <sdbus-c++/sdbus-c++.h>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

namespace edge {

namespace {
volatile std::sig_atomic_t g_shutdown_signal = 0;
// fd written by the signal handler to break the main-loop poll() immediately.
// Set by setup_signal_handlers(), cleared in the destructor. std::atomic<int>
// load is lock-free (async-signal-safe) on all supported targets.
std::atomic<int> g_wakeup_fd{-1};

void handle_shutdown_signal(int) {
    g_shutdown_signal = 1;
    // Only async-signal-safe calls here: an atomic load and write(2).
    const int fd = g_wakeup_fd.load(std::memory_order_relaxed);
    if (fd >= 0) {
        const uint64_t one = 1;
        const ssize_t r = write(fd, &one, sizeof(one));
        (void)r;  // best-effort wakeup; nothing safe to do on failure
    }
}

int64_t monotonic_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}
} // namespace

namespace detail {

bool wait_wakeup_fd(int fd, std::chrono::milliseconds timeout) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    const auto count = timeout.count();
    const int ms = count > INT_MAX ? INT_MAX : static_cast<int>(count);

    // nfds=0 (fd < 0) degrades to a plain timed sleep — still bounded by timeout.
    const int n = poll(&pfd, fd >= 0 ? 1 : 0, ms);
    if (n > 0 && (pfd.revents & POLLIN)) {
        // Drain the eventfd counter so the next poll() blocks again. One read
        // clears an EFD_NONBLOCK eventfd; the loop just tolerates pipe semantics.
        uint64_t drained = 0;
        while (read(fd, &drained, sizeof(drained)) ==
               static_cast<ssize_t>(sizeof(drained))) {
        }
        return true;
    }
    // n < 0 → interrupted by a signal (EINTR): report a wakeup so the caller
    // re-checks shutdown/trigger state. n == 0 → timeout.
    return n < 0;
}

bool watchdog_sleep(std::condition_variable_any& cv, std::stop_token st,
                    std::chrono::nanoseconds timeout) {
    // A private mutex satisfies the wait_for contract; there is no shared state
    // to guard — the stop-callback registered by this overload notifies `cv`.
    std::mutex m;
    std::unique_lock lock(m);
    return cv.wait_for(lock, st, timeout,
                       [&st] { return st.stop_requested(); });
}

} // namespace detail

// -----------------------------------------------------------------------------
// SnapshotDaemon implementation
// -----------------------------------------------------------------------------

SnapshotDaemon::SnapshotDaemon(Config config)
    : config_(std::move(config)) {
    // eventfd woken by the SIGTERM/SIGINT handler (async-signal-safe write) and
    // the D-Bus trigger callbacks, so the main wait in run() breaks within ~1s
    // instead of waiting out collect_interval. Non-blocking so draining the
    // counter never stalls the loop.
    wakeup_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

SnapshotDaemon::~SnapshotDaemon() {
    stop_watchdog_thread();
    if (running_.load()) {
        request_shutdown();
    }
    // Detach the handler from the fd before closing so a late signal can't
    // write into a closed (or recycled) descriptor.
    g_wakeup_fd.store(-1, std::memory_order_relaxed);
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

std::optional<std::string> SnapshotDaemon::initialize() {
    // Create state directory if needed
    std::error_code ec;
    std::filesystem::create_directories(config_.state_dir, ec);
    if (ec) {
        return "Failed to create state directory: " + ec.message();
    }

    if (wakeup_fd_ < 0) {
        log::warn("eventfd creation failed; shutdown/trigger may be delayed up to collect_interval");
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
                wake();
                return true;
            },
            // AcknowledgeCrash: record the ack only if the caller's fingerprint
            // matches the daemon's current panic fingerprint. CrashProbe stays
            // read-only; this is the sole writer of crash_state.json. Written
            // atomically so a power cut can't leave a half-written ack.
            [this](const std::string& fingerprint) -> bool {
                {
                    std::lock_guard lock(crash_fp_mutex_);
                    if (!current_panic_fingerprint_ ||
                        *current_panic_fingerprint_ != fingerprint) {
                        return false;
                    }
                }
                const auto state_path = config_.state_dir / "crash_state.json";
                nlohmann::json j;
                j["acknowledged_fingerprint"] = fingerprint;
                auto res = atomic_write_file(state_path, j.dump(2));
                if (!res) {
                    log::probe_error("crash",
                        "failed to persist crash acknowledgement: " +
                        res.error().what());
                    return false;
                }
                // Wake a cycle so the acknowledged state is reflected promptly
                // (severity drops, alarm latch clears) without waiting for the
                // regular crash poll interval. crash_ack_pending_ forces the
                // schedule-gated crash probe due on that woken cycle (mirrors the
                // RAUC path); a bare wake alone would re-aggregate stale crash data.
                crash_ack_pending_.store(true, std::memory_order_relaxed);
                trigger_requested_.store(true);
                wake();
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
                    trigger_requested_.store(true);
                    wake();
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

    // Persistent journal reader: open the journal once and prime the buffer, then
    // every cycle only ingests new entries. The Services and Journal probes read
    // from it instead of opening the journal per unit per cycle. A failed open
    // leaves the reader degraded (journal fields empty/Ok), as before.
    journal_reader_ = std::make_unique<JournalReader>(config_);
    if (!journal_reader_->init()) {
        log::warn("journal reader unavailable; journal severity and log excerpts "
                  "will be empty");
    }

    device_probe_ = std::make_unique<DeviceProbe>(config_);
    boot_probe_ = std::make_unique<BootProbe>(config_, config_.state_dir);
    services_probe_ = std::make_unique<ServicesProbe>(
        config_, dbus, config_.monitored_services, journal_reader_.get());
    resources_probe_ = std::make_unique<ResourcesProbe>(
        config_,
        *nl_monitor_,
        std::span<const std::string>(config_.monitored_mounts),
        std::span<const std::string>(config_.monitored_interfaces)
    );
    time_sync_probe_ = std::make_unique<TimeSyncProbe>(config_, dbus);
    update_probe_ = std::make_unique<UpdateProbe>(config_, dbus);
    journal_probe_ = std::make_unique<JournalProbe>(config_, journal_reader_.get());
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

    // Main loop — wait for one collect_interval OR an early wakeup.
    // The wait blocks in poll() on wakeup_fd_ (an eventfd). The SIGTERM/SIGINT
    // handler and the D-Bus TriggerSnapshot/RAUC callbacks all write the eventfd,
    // so a shutdown or trigger breaks the wait within ~1s instead of waiting out
    // the full collect_interval (previously up to collect_interval of latency,
    // risking systemd's TimeoutStopSec SIGKILL).
    const auto interval_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(config_.collect_interval);
    while (!shutdown_requested_.load()) {
        if (g_shutdown_signal != 0) {
            request_shutdown();
            break;
        }

        (void)detail::wait_wakeup_fd(wakeup_fd_, interval_ms);
        trigger_requested_.store(false);

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
    wake();  // break the main-loop poll() immediately
    if (dbus_connection_) {
        dbus_connection_->leaveEventLoop();
    }
}

void SnapshotDaemon::wake() noexcept {
    if (wakeup_fd_ < 0) {
        return;
    }
    const uint64_t one = 1;
    const ssize_t r = ::write(wakeup_fd_, &one, sizeof(one));
    (void)r;  // best-effort; EAGAIN just means a wakeup is already pending
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

    // Refresh the persistent sources before collection: drain netlink events and
    // pull new journal entries into the in-memory buffer (one sd_journal_process,
    // no per-cycle open).
    if (nl_monitor_) {
        nl_monitor_->drain_events();
    }
    if (journal_reader_) {
        journal_reader_->ingest();
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
            // Stamp freshness on sections that carry it (all but DeviceInfo).
            if constexpr (requires { cache.freshness; }) {
                cache.freshness.collected_at = std::chrono::system_clock::now();
                cache.freshness.stale = false;
            }
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

    // If a crash was acknowledged via D-Bus since the last cycle, force the crash
    // probe due immediately so the ack (severity drop, alarm clear) is reflected
    // on this woken cycle instead of at the next scheduled crash poll. Mirrors the
    // RAUC path above; clear before collect so a concurrent ack is not lost.
    if (crash_ack_pending_.exchange(false, std::memory_order_relaxed)) {
        crash_schedule_.next_run = now;
    }
    maybe_collect(*crash_probe_,     crash_schedule_,     last_known_good_.crash,     "crash");

    // Mark a section stale if it was collected before but is now past its
    // freshness window (one missed cadence, 30s floor). A never-collected section
    // (has_result=false) is NOT marked stale — it stays severity unknown, so a
    // warm-up / absent dependency does not raise the roll-up.
    auto mark_stale = [&](const ProbeSchedule& sched, auto& section) {
        if (sched.has_result && sched.interval.count() > 0) {
            const auto grace = std::max(sched.interval, std::chrono::seconds{30});
            section.freshness.stale = now > sched.next_run + grace;
        }
    };
    mark_stale(boot_schedule_,      last_known_good_.boot);
    mark_stale(services_schedule_,  last_known_good_.services);
    mark_stale(resources_schedule_, last_known_good_.resources);
    mark_stale(time_sync_schedule_, last_known_good_.time_sync);
    mark_stale(update_schedule_,    last_known_good_.update);
    mark_stale(journal_schedule_,   last_known_good_.journal);
    mark_stale(crash_schedule_,     last_known_good_.crash);

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

    // Write to file. Serialization can, in principle, still throw (e.g. a future
    // field or an nlohmann edge case the UTF-8 replace handler doesn't cover);
    // contain it here so one bad snapshot never terminates the daemon and turns
    // into a restart crash-loop. Continue with the previous good state on disk.
    try {
        if (auto result = writer_->write(state); !result) {
            log::writer_error("Failed to write snapshot: " + result.error().message);
        }
    } catch (const std::exception& ex) {
        log::writer_error(std::string("Snapshot serialization failed: ") + ex.what());
    }

    // Update current state and capture severity transition
    Severity prev_sev = last_severity_;
    {
        std::lock_guard lock(state_mutex_);
        current_state_ = std::move(state);
        last_severity_ = current_state_.summary.severity;
    }
    const Severity new_sev = last_severity_;

    // Publish the current panic fingerprint for the AcknowledgeCrash callback.
    // nullopt when there are no kernel-fault dumps, so an ack against a drained
    // pstore is rejected.
    {
        std::lock_guard lock(crash_fp_mutex_);
        current_panic_fingerprint_ =
            (current_state_.crash.panic_count > 0) ? current_state_.crash.fingerprint
                                                   : std::nullopt;
    }

    // Push severity and cached logs to D-Bus manager.
    if (health_manager_) {
        health_manager_->update_severity(new_sev, prev_sev);
        health_manager_->update_recent_logs(current_state_.journal.recent_errors);

        // Distinguishable crash alarm: fire only for a NEW unacknowledged
        // CURRENT-BOOT kernel panic (the crit-driving case). Prior-boot or
        // acknowledged panics don't alarm. Clear/re-arm the latch when the panic
        // is acknowledged, ages out to prior-boot only, or pstore is drained, so
        // a genuinely new fresh panic re-alarms.
        const auto& crash = current_state_.crash;
        const bool active_panic = crash.panic_count > 0 &&
                                  !crash.acknowledged &&
                                  crash.panic_current_boot &&
                                  crash.fingerprint.has_value();
        if (active_panic) {
            if (last_alarmed_crash_fp_ != crash.fingerprint) {
                std::string msg = "kernel panic detected this boot: " +
                                  std::to_string(crash.panic_count) +
                                  " panic artifact(s), fingerprint=" +
                                  *crash.fingerprint;
                health_manager_->emit_alarm("crash", msg, Severity::Crit);
                last_alarmed_crash_fp_ = crash.fingerprint;
            }
        } else {
            last_alarmed_crash_fp_.reset();
        }
    }

    // Log snapshot with severity + the degraded domains, so a plain journalctl
    // view names the subsystem to look at, not just the roll-up.
    log::snapshot_collected(to_string(new_sev),
                            degraded_domains(current_state_.summary.domains));

    // Keep systemctl status current: version + live severity visible without journalctl
    systemd::notify_status(EDGE_HEALTHD_VERSION, to_string(new_sev));

    update_watchdog_heartbeat();
}

void SnapshotDaemon::setup_signal_handlers() {
    // Publish the wakeup fd so the async-signal-safe handler can break poll().
    g_wakeup_fd.store(wakeup_fd_, std::memory_order_relaxed);
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
    // request_stop() fires the condition_variable_any stop-callback registered
    // in watchdog_sleep(), so the loop returns immediately and the jthread
    // destructor's auto-join does not wait out WatchdogSec/2.
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

    // Interruptible sleep: request_stop() notifies watchdog_cv_ via the
    // condition_variable_any stop-callback, so watchdog_sleep() returns true
    // immediately and join() on shutdown is fast (was an uninterruptible
    // sleep_for that blocked join for up to WatchdogSec/2). Only ping while the
    // heartbeat is fresh (age <= timeout), preserving the previous behaviour.
    while (!detail::watchdog_sleep(watchdog_cv_, st, interval)) {
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
