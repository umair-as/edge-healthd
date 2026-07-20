// SPDX-License-Identifier: MIT
// edge-healthd: persistent journal reader
//
// Opens the system journal ONCE and keeps the handle open (sd_journal follows
// log rotation while open), maintaining a bounded in-memory ring buffer of
// recent entries. Every journal-derived snapshot field — journal.overall /
// error_count / recent_errors, per-unit services log_excerpt, and the D-Bus
// GetRecentLogs cache — is computed from this buffer, so a collection cycle
// costs one sd_journal_process() plus a read of only the new entries, instead of
// re-opening and re-walking every rotated journal file per unit per cycle.

#pragma once

#include "config.hpp"
#include "journal.hpp"   // JournalEntry, filter_journal_entries, sanitize_utf8
#include "types.hpp"     // Severity

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct sd_journal;

namespace edge {

// Pure buffer operations, factored out so they can be unit-tested against a
// synthetic entry buffer without a live sd_journal. JournalReader delegates to
// these over its internal buffer. Buffers are chronological (oldest at front).
namespace journal_detail {

// Drop entries beyond the count cap (config, or 1000) and older than the age
// bound (>= max(log_excerpt_window_sec, 300s)).
void evict(const Config& cfg, std::deque<JournalEntry>& buffer);

// Count of error-priority (<= min_priority, default 3) entries in the window.
[[nodiscard]] uint32_t error_count(const Config& cfg,
                                   const std::deque<JournalEntry>& buffer);
// Crit if any priority <= 2 in the window, Warn if any <= 3, else Ok.
[[nodiscard]] Severity overall_severity(const Config& cfg,
                                        const std::deque<JournalEntry>& buffer);
// Formatted recent error lines (newest-first, via filter_journal_entries).
[[nodiscard]] std::vector<std::string> recent_errors(
    const Config& cfg, const std::deque<JournalEntry>& buffer);
// Recent lines for one unit (all priorities), best-effort.
[[nodiscard]] std::vector<std::string> excerpt_for_unit(
    const Config& cfg, const std::deque<JournalEntry>& buffer,
    const std::string& unit);

} // namespace journal_detail

class JournalReader {
public:
    explicit JournalReader(const Config& config);
    ~JournalReader();
    JournalReader(const JournalReader&) = delete;
    JournalReader& operator=(const JournalReader&) = delete;

    // Open the journal and prime the buffer from recent history. Returns false
    // if the journal cannot be opened; the reader then stays degraded and all
    // queries return empty / Ok (graceful, matching the previous absent-journal
    // behaviour). Idempotent enough to be called once at startup.
    bool init();

    // True once the handle is open.
    [[nodiscard]] bool available() const { return journal_ != nullptr; }

    // Catch up on entries appended since the last call and evict out-of-bounds
    // entries. Cheap; performs no open. No-op when degraded. Call once per cycle.
    void ingest();

    // --- Queries over the in-memory buffer (no journal I/O) ---

    // Count of error-priority entries currently in the buffer window.
    [[nodiscard]] uint32_t error_count() const;
    // Roll-up severity: Crit if any priority <= 2, Warn if any <= 3, else Ok.
    [[nodiscard]] Severity overall_severity() const;
    // Formatted recent error lines (filter_journal_entries over error entries).
    [[nodiscard]] std::vector<std::string> recent_errors() const;
    // Recent lines for a specific unit (all priorities), best-effort: empty if
    // the unit has aged out of the buffer.
    [[nodiscard]] std::vector<std::string> excerpt_for_unit(const std::string& unit) const;

private:
    const Config& config_;
    sd_journal* journal_ = nullptr;
    std::deque<JournalEntry> buffer_;

    // Buffer count cap (config override or 1000).
    [[nodiscard]] size_t max_entries_() const;
    // Read the entry at the current journal cursor into `out`. Returns false on
    // a read error.
    bool read_current(JournalEntry& out) const;
    // Drop entries beyond the count cap and older than the age bound.
    void evict();
    // Re-seek to the tail and refill the buffer from the last N entries. Used to
    // bound catch-up work when the reader has fallen far behind.
    void rebuild_from_tail();
};

} // namespace edge
