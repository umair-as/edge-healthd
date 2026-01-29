// SPDX-License-Identifier: MIT
#pragma once

#include "config.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace edge {

struct JournalEntry {
    uint64_t realtime_usec = 0; // microseconds since epoch
    int priority = 6; // syslog priority: 0..7 (default info)
    std::string message;
};

// Filter raw journal entries according to config: window_sec, min_priority, max_lines.
// Returns ordered log lines (chronological, newest last).
std::vector<std::string> filter_journal_entries(const Config& cfg,
                                                const std::vector<JournalEntry>& entries);

} // namespace edge
