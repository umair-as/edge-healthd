// SPDX-License-Identifier: MIT
#include "journal.hpp"
#include <algorithm>

namespace edge {

std::vector<std::string> filter_journal_entries(const Config& cfg,
                                                const std::vector<JournalEntry>& entries) {
    std::vector<std::string> out;
    if (entries.empty()) return out;

    // Determine window usage
    bool use_window = cfg.log_excerpt_window_sec && (*cfg.log_excerpt_window_sec > 0);
    uint64_t cutoff_usec = 0;
    if (use_window) {
        const auto now = std::chrono::system_clock::now();
        const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        cutoff_usec = static_cast<uint64_t>(now_us) - (*cfg.log_excerpt_window_sec * 1000000ULL);
    }

    // Iterate entries (assume entries provided in reverse-chronological order (newest first))
    // We'll collect up to max_lines, applying priority and window filters, then reverse to chronological order.
    const size_t max_lines = cfg.log_excerpt_max_lines ? cfg.log_excerpt_max_lines : 20;
    for (const auto& e : entries) {
        if (use_window && e.realtime_usec < cutoff_usec) continue;
        if (cfg.log_excerpt_min_priority) {
            if (!(e.priority <= *cfg.log_excerpt_min_priority)) continue;
        }
        // Trim trailing newlines
        std::string msg = e.message;
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
        // Truncate long lines to 512 chars
        if (msg.size() > 512) msg.resize(512);
        if (!msg.empty()) out.push_back(std::move(msg));
        if (out.size() >= max_lines) break;
    }

    // Entries are returned newest->oldest (most recent first)
    return out;
}

} // namespace edge
