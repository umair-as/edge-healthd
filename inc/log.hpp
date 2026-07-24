// SPDX-License-Identifier: MIT
// edge-healthd: Logging (single-header implementation)
//
// Features:
// - Thread-safe with atomic level
// - ISO 8601 timestamps in stderr mode
// - Structured journal fields when available
// - Zero-allocation fast path for filtered messages

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <unistd.h>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#endif

namespace edge::log {

// -----------------------------------------------------------------------------
// Log levels
// -----------------------------------------------------------------------------

enum class Level : int {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

[[nodiscard]] constexpr std::string_view to_string(Level level) noexcept {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

// -----------------------------------------------------------------------------
// Global state (inline variables - C++17)
// -----------------------------------------------------------------------------

namespace detail {

inline std::atomic<Level> g_level{Level::Info};
inline bool g_color_enabled{false};
inline std::mutex g_mutex;
inline std::mutex g_rate_mutex;

inline void init_color() {
    g_color_enabled = ::isatty(STDERR_FILENO) != 0;
}

struct ColorPair {
    std::string_view on;
    std::string_view off;
};

inline ColorPair level_color(Level level) noexcept {
    if (!g_color_enabled) {
        return {{}, {}};
    }
    switch (level) {
        case Level::Debug: return {"\033[36m",   "\033[0m"};
        case Level::Info:  return {"\033[32m",   "\033[0m"};
        case Level::Warn:  return {"\033[33m",   "\033[0m"};
        case Level::Error: return {"\033[1;31m", "\033[0m"};
    }
    return {{}, {}};
}

inline std::string_view dim_code() noexcept {
    return g_color_enabled ? "\033[2m" : "";
}

inline std::string_view reset_code() noexcept {
    return g_color_enabled ? "\033[0m" : "";
}

inline constexpr auto k_probe_error_interval = std::chrono::seconds(60);
inline constexpr auto k_snapshot_interval = std::chrono::minutes(5);
inline constexpr auto k_writer_error_interval = std::chrono::seconds(60);

struct RateLimiter {
    std::chrono::steady_clock::time_point last =
        std::chrono::steady_clock::time_point::min();
    uint32_t suppressed = 0;
};

inline bool allow_rate_limited(RateLimiter& limiter,
                               std::chrono::steady_clock::duration interval,
                               uint32_t* suppressed_out) {
    const auto now = std::chrono::steady_clock::now();
    if (limiter.last == std::chrono::steady_clock::time_point::min() ||
        now - limiter.last >= interval) {
        if (suppressed_out) {
            *suppressed_out = limiter.suppressed;
        }
        limiter.suppressed = 0;
        limiter.last = now;
        return true;
    }

    limiter.suppressed++;
    return false;
}

// Format current time as ISO 8601
inline std::string format_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm{};
    gmtime_r(&time_t, &tm);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long>(ms.count()));
    return buf;
}

#ifdef EDGE_HAS_SYSTEMD
inline int level_to_priority(Level level) noexcept {
    switch (level) {
        case Level::Debug: return LOG_DEBUG;
        case Level::Info:  return LOG_INFO;
        case Level::Warn:  return LOG_WARNING;
        case Level::Error: return LOG_ERR;
    }
    return LOG_INFO;
}
#endif

inline std::string format_field(std::string_view key, std::string_view value) {
    std::string field;
    field.reserve(key.size() + 1 + value.size());
    field.append(key);
    field.push_back('=');
    field.append(value);
    return field;
}

inline void write_message(Level level, std::string_view msg) {
    std::lock_guard lock(g_mutex);

#ifdef EDGE_HAS_SYSTEMD
    sd_journal_send(
        "MESSAGE=%.*s", static_cast<int>(msg.size()), msg.data(),
        "PRIORITY=%d", level_to_priority(level),
        "SYSLOG_IDENTIFIER=edge-healthd",
        nullptr);
#else
    auto level_str = to_string(level);
    auto [con, coff] = level_color(level);
    auto dim = dim_code();
    auto rst = reset_code();
    std::fprintf(stderr, "%.*s%s%.*s [%.*s%-5.*s%.*s] %.*s\n",
                 static_cast<int>(dim.size()), dim.data(),
                 format_timestamp().c_str(),
                 static_cast<int>(rst.size()), rst.data(),
                 static_cast<int>(con.size()), con.data(),
                 static_cast<int>(level_str.size()), level_str.data(),
                 static_cast<int>(coff.size()), coff.data(),
                 static_cast<int>(msg.size()), msg.data());
#endif
}

inline void write_structured(Level level, std::string_view msg,
                             std::string_view key1, std::string_view val1,
                             std::string_view key2 = {}, std::string_view val2 = {}) {
    std::lock_guard lock(g_mutex);

#ifdef EDGE_HAS_SYSTEMD
    auto field1 = format_field(key1, val1);
    if (key2.empty()) {
        sd_journal_send(
            "MESSAGE=%.*s", static_cast<int>(msg.size()), msg.data(),
            "PRIORITY=%d", level_to_priority(level),
            "SYSLOG_IDENTIFIER=edge-healthd",
            "%s", field1.c_str(),
            nullptr);
    } else {
        auto field2 = format_field(key2, val2);
        sd_journal_send(
            "MESSAGE=%.*s", static_cast<int>(msg.size()), msg.data(),
            "PRIORITY=%d", level_to_priority(level),
            "SYSLOG_IDENTIFIER=edge-healthd",
            "%s", field1.c_str(),
            "%s", field2.c_str(),
            nullptr);
    }
#else
    auto level_str = to_string(level);
    auto [con, coff] = level_color(level);
    auto dim = dim_code();
    auto rst = reset_code();
    if (key2.empty()) {
        std::fprintf(stderr, "%.*s%s%.*s [%.*s%-5.*s%.*s] %.*s %.*s=%.*s\n",
                     static_cast<int>(dim.size()), dim.data(),
                     format_timestamp().c_str(),
                     static_cast<int>(rst.size()), rst.data(),
                     static_cast<int>(con.size()), con.data(),
                     static_cast<int>(level_str.size()), level_str.data(),
                     static_cast<int>(coff.size()), coff.data(),
                     static_cast<int>(msg.size()), msg.data(),
                     static_cast<int>(key1.size()), key1.data(),
                     static_cast<int>(val1.size()), val1.data());
    } else {
        std::fprintf(stderr, "%.*s%s%.*s [%.*s%-5.*s%.*s] %.*s %.*s=%.*s %.*s=%.*s\n",
                     static_cast<int>(dim.size()), dim.data(),
                     format_timestamp().c_str(),
                     static_cast<int>(rst.size()), rst.data(),
                     static_cast<int>(con.size()), con.data(),
                     static_cast<int>(level_str.size()), level_str.data(),
                     static_cast<int>(coff.size()), coff.data(),
                     static_cast<int>(msg.size()), msg.data(),
                     static_cast<int>(key1.size()), key1.data(),
                     static_cast<int>(val1.size()), val1.data(),
                     static_cast<int>(key2.size()), key2.data(),
                     static_cast<int>(val2.size()), val2.data());
    }
#endif
}

} // namespace detail

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

inline void init() {
    detail::init_color();
}

inline void set_level(Level level) noexcept {
    detail::g_level.store(level, std::memory_order_relaxed);
}

[[nodiscard]] inline Level get_level() noexcept {
    return detail::g_level.load(std::memory_order_relaxed);
}

[[nodiscard]] inline bool should_log(Level level) noexcept {
    return static_cast<int>(level) >= static_cast<int>(get_level());
}

// Core logging functions
inline void debug(std::string_view msg) {
    if (should_log(Level::Debug)) {
        detail::write_message(Level::Debug, msg);
    }
}

inline void info(std::string_view msg) {
    if (should_log(Level::Info)) {
        detail::write_message(Level::Info, msg);
    }
}

inline void warn(std::string_view msg) {
    if (should_log(Level::Warn)) {
        detail::write_message(Level::Warn, msg);
    }
}

inline void error(std::string_view msg) {
    if (should_log(Level::Error)) {
        detail::write_message(Level::Error, msg);
    }
}

// -----------------------------------------------------------------------------
// Structured logging for observability events
// -----------------------------------------------------------------------------

// Heartbeat log for each collection cycle. `severity` is the roll-up; `degraded`
// is an optional "domain=sev, …" list of the subsystems worse than ok, folded
// into the human-readable MESSAGE so a plain `journalctl` view is self-describing
// (the machine-parseable SEVERITY field is kept). Rate-limited to k_snapshot_
// interval per *severity* — a severity change logs immediately; within-severity
// cycles are suppressed — so the message stays keyed to the roll-up transition.
inline void snapshot_collected(std::string_view severity,
                               std::string_view degraded = {}) {
    if (should_log(Level::Info)) {
        static std::string last_severity;
        static auto last_log = std::chrono::steady_clock::time_point::min();
        bool allow = false;

        {
            std::lock_guard lock(detail::g_rate_mutex);
            const auto now = std::chrono::steady_clock::now();
            if (last_severity != severity) {
                last_severity.assign(severity.data(), severity.size());
                last_log = now;
                allow = true;
            } else if (last_log == std::chrono::steady_clock::time_point::min() ||
                       now - last_log >= detail::k_snapshot_interval) {
                last_log = now;
                allow = true;
            }
        }

        if (allow) {
            std::string message = "Snapshot collected: ";
            message.append(severity.data(), severity.size());
            if (!degraded.empty()) {
                message += " (";
                message.append(degraded.data(), degraded.size());
                message += ')';
            }
            detail::write_structured(Level::Info, message,
                                     "SEVERITY", severity);
        }
    }
}

inline void probe_error(std::string_view probe, std::string_view error_msg) {
    if (should_log(Level::Warn)) {
        static detail::RateLimiter device_limiter;
        static detail::RateLimiter boot_limiter;
        static detail::RateLimiter services_limiter;
        static detail::RateLimiter resources_limiter;
        static detail::RateLimiter time_sync_limiter;
        static detail::RateLimiter update_limiter;
        static detail::RateLimiter other_limiter;

        detail::RateLimiter* limiter = &other_limiter;
        if (probe == "device") {
            limiter = &device_limiter;
        } else if (probe == "boot") {
            limiter = &boot_limiter;
        } else if (probe == "services") {
            limiter = &services_limiter;
        } else if (probe == "resources") {
            limiter = &resources_limiter;
        } else if (probe == "time_sync") {
            limiter = &time_sync_limiter;
        } else if (probe == "update") {
            limiter = &update_limiter;
        }

        uint32_t suppressed = 0;
        bool allow = false;
        {
            std::lock_guard lock(detail::g_rate_mutex);
            allow = detail::allow_rate_limited(*limiter,
                                               detail::k_probe_error_interval,
                                               &suppressed);
        }

        if (!allow) {
            return;
        }

        std::string message;
        message.reserve(64 + probe.size() + error_msg.size());
        message = "Probe failed [";
        message.append(probe.data(), probe.size());
        message += "]: ";
        message.append(error_msg.data(), error_msg.size());

        if (suppressed > 0) {
            message += " (suppressed ";
            message += std::to_string(suppressed);
            message += ")";
            detail::write_structured(Level::Warn, message,
                                     "PROBE", probe,
                                     "ERROR", error_msg);
        } else {
            detail::write_structured(Level::Warn, message,
                                     "PROBE", probe,
                                     "ERROR", error_msg);
        }
    }
}

inline void writer_error(std::string_view msg) {
    if (should_log(Level::Error)) {
        static detail::RateLimiter limiter;
        bool allow = false;
        {
            std::lock_guard lock(detail::g_rate_mutex);
            allow = detail::allow_rate_limited(limiter,
                                               detail::k_writer_error_interval,
                                               nullptr);
        }
        if (allow) {
            detail::write_message(Level::Error, msg);
        }
    }
}

inline void daemon_starting(std::string_view version) {
    if (should_log(Level::Info)) {
        detail::write_structured(Level::Info, "Daemon starting",
                                 "VERSION", version);
    }
}

inline void daemon_ready() {
    info("Daemon ready");
}

inline void daemon_stopping() {
    info("Daemon stopping");
}

} // namespace edge::log
