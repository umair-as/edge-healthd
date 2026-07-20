// SPDX-License-Identifier: MIT
#pragma once

#include "config.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace edge {

// Replace any byte sequence that is not well-formed UTF-8 with the Unicode
// replacement character (U+FFFD). Journal MESSAGE fields and systemd unit log
// text are arbitrary bytes and are frequently not valid UTF-8; feeding such
// bytes to nlohmann::json::dump() (strict handler) throws type_error(316) and,
// left uncaught, crash-loops the daemon. Sanitizing at capture is defense in
// depth on top of the replace error handler used during serialization.
std::string sanitize_utf8(std::string_view input);

struct JournalEntry {
    uint64_t realtime_usec = 0; // microseconds since epoch
    int priority = 6; // syslog priority: 0..7 (default info)
    std::string message;
    // Unit-association fields, captured so per-unit excerpts match the same way
    // `journalctl -u <unit>` does — a message may carry any of these.
    std::string unit;        // _SYSTEMD_UNIT (message emitted BY the unit's process)
    std::string unit_hint;   // UNIT (systemd's own messages ABOUT the unit)
    std::string syslog_id;   // SYSLOG_IDENTIFIER (program name)

    // True when this entry belongs to `name` under journalctl -u semantics.
    [[nodiscard]] bool matches_unit(std::string_view name) const {
        return unit == name || unit_hint == name || syslog_id == name;
    }
};

// Filter raw journal entries according to config: window_sec, min_priority, max_lines.
// Returns ordered log lines (chronological, newest last).
std::vector<std::string> filter_journal_entries(const Config& cfg,
                                                const std::vector<JournalEntry>& entries);

} // namespace edge
