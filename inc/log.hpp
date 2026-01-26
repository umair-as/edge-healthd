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
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

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
inline std::mutex g_mutex;

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
    std::fprintf(stderr, "%s [%-5.*s] %.*s\n",
                 format_timestamp().c_str(),
                 static_cast<int>(level_str.size()), level_str.data(),
                 static_cast<int>(msg.size()), msg.data());
#endif
}

inline void write_structured(Level level, std::string_view msg,
                             std::string_view key1, std::string_view val1,
                             std::string_view key2 = {}, std::string_view val2 = {}) {
    std::lock_guard lock(g_mutex);

#ifdef EDGE_HAS_SYSTEMD
    if (key2.empty()) {
        sd_journal_send(
            "MESSAGE=%.*s", static_cast<int>(msg.size()), msg.data(),
            "PRIORITY=%d", level_to_priority(level),
            "SYSLOG_IDENTIFIER=edge-healthd",
            "%.*s=%.*s",
            static_cast<int>(key1.size()), key1.data(),
            static_cast<int>(val1.size()), val1.data(),
            nullptr);
    } else {
        sd_journal_send(
            "MESSAGE=%.*s", static_cast<int>(msg.size()), msg.data(),
            "PRIORITY=%d", level_to_priority(level),
            "SYSLOG_IDENTIFIER=edge-healthd",
            "%.*s=%.*s",
            static_cast<int>(key1.size()), key1.data(),
            static_cast<int>(val1.size()), val1.data(),
            "%.*s=%.*s",
            static_cast<int>(key2.size()), key2.data(),
            static_cast<int>(val2.size()), val2.data(),
            nullptr);
    }
#else
    auto level_str = to_string(level);
    if (key2.empty()) {
        std::fprintf(stderr, "%s [%-5.*s] %.*s %.*s=%.*s\n",
                     format_timestamp().c_str(),
                     static_cast<int>(level_str.size()), level_str.data(),
                     static_cast<int>(msg.size()), msg.data(),
                     static_cast<int>(key1.size()), key1.data(),
                     static_cast<int>(val1.size()), val1.data());
    } else {
        std::fprintf(stderr, "%s [%-5.*s] %.*s %.*s=%.*s %.*s=%.*s\n",
                     format_timestamp().c_str(),
                     static_cast<int>(level_str.size()), level_str.data(),
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

inline void snapshot_collected(std::string_view severity) {
    if (should_log(Level::Info)) {
        detail::write_structured(Level::Info, "Snapshot collected",
                                 "SEVERITY", severity);
    }
}

inline void probe_error(std::string_view probe, std::string_view error_msg) {
    if (should_log(Level::Warn)) {
        detail::write_structured(Level::Warn, "Probe failed",
                                 "PROBE", probe,
                                 "ERROR", error_msg);
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
