// SPDX-License-Identifier: MIT
// edge-healthd: persistent journal reader

#include "journal_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#endif

namespace edge {

namespace {
constexpr uint64_t kUsecPerSec = 1000000ULL;

uint64_t now_usec() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());
}

int error_priority(const Config& cfg) {
    return cfg.log_excerpt_min_priority.value_or(3); // 3 = err
}

size_t buffer_cap(const Config& cfg) {
    return cfg.journal_buffer_max_entries ? cfg.journal_buffer_max_entries : 1000;
}

// True when the entry is within the configured reporting window (all entries
// pass when no window is set).
bool in_window(const Config& cfg, const JournalEntry& e) {
    if (!cfg.log_excerpt_window_sec || *cfg.log_excerpt_window_sec == 0) return true;
    const uint64_t cutoff = now_usec() - *cfg.log_excerpt_window_sec * kUsecPerSec;
    return e.realtime_usec >= cutoff;
}
} // namespace

// --- journal_detail: pure buffer operations (unit-testable) ------------------

namespace journal_detail {

void evict(const Config& cfg, std::deque<JournalEntry>& buffer) {
    // Count-bound only: keep the last N entries (like the old fixed-size
    // back-scan). This caps memory and preserves recent history for quiet units
    // and sparse errors; an explicit reporting window, when configured, is
    // applied at query time by filter_journal_entries, not by eviction. (An age
    // bound here would drop a quiet unit's boot-time logs and empty its excerpt,
    // regressing the previous behaviour.)
    const size_t cap = buffer_cap(cfg);
    while (buffer.size() > cap) buffer.pop_front();
}

uint32_t error_count(const Config& cfg, const std::deque<JournalEntry>& buffer) {
    uint32_t n = 0;
    for (const auto& e : buffer) {
        if (e.priority <= error_priority(cfg) && in_window(cfg, e)) ++n;
    }
    return n;
}

Severity overall_severity(const Config& cfg, const std::deque<JournalEntry>& buffer) {
    bool crit = false, warn = false;
    for (const auto& e : buffer) {
        if (!in_window(cfg, e)) continue;
        if (e.priority <= 2) { crit = true; break; }
        if (e.priority <= 3) warn = true;
    }
    if (crit) return Severity::Crit;
    if (warn) return Severity::Warn;
    return Severity::Ok;
}

std::vector<std::string> recent_errors(const Config& cfg,
                                       const std::deque<JournalEntry>& buffer) {
    // Newest-first error entries → filter_journal_entries (window/max_lines).
    std::vector<JournalEntry> errs;
    for (auto it = buffer.rbegin(); it != buffer.rend(); ++it) {
        if (it->priority <= error_priority(cfg)) errs.push_back(*it);
    }
    return filter_journal_entries(cfg, errs);
}

std::vector<std::string> excerpt_for_unit(const Config& cfg,
                                          const std::deque<JournalEntry>& buffer,
                                          const std::string& unit) {
    // Newest-first entries for this unit (all priorities), best-effort. Match
    // _SYSTEMD_UNIT / UNIT / SYSLOG_IDENTIFIER, mirroring journalctl -u.
    std::vector<JournalEntry> lines;
    for (auto it = buffer.rbegin(); it != buffer.rend(); ++it) {
        if (it->matches_unit(unit)) lines.push_back(*it);
    }
    return filter_journal_entries(cfg, lines);
}

} // namespace journal_detail

JournalReader::JournalReader(const Config& config) : config_(config) {}

JournalReader::~JournalReader() {
#ifdef EDGE_HAS_SYSTEMD
    if (journal_) sd_journal_close(journal_);
#endif
    journal_ = nullptr;
}

size_t JournalReader::max_entries_() const {
    return buffer_cap(config_);
}

#ifdef EDGE_HAS_SYSTEMD

bool JournalReader::read_current(JournalEntry& out) const {
    // Return the value of journal field `name` (text after the "name=" prefix),
    // or empty if the field is absent on this entry.
    const auto read_field = [this](const char* name) -> std::string {
        const void* d = nullptr;
        size_t l = 0;
        if (sd_journal_get_data(journal_, name, &d, &l) < 0 || !d) return {};
        const char* p = static_cast<const char*>(d);
        const size_t pre = std::strlen(name) + 1; // "name="
        if (l > pre && std::strncmp(p, name, pre - 1) == 0 && p[pre - 1] == '=') {
            p += pre;
            l -= pre;
        }
        return std::string(p, l);
    };

    uint64_t usec = 0;
    if (sd_journal_get_realtime_usec(journal_, &usec) < 0) usec = 0;
    out.realtime_usec = usec;

    out.priority = 6; // default info
    const void* data = nullptr;
    size_t len = 0;
    if (sd_journal_get_data(journal_, "PRIORITY", &data, &len) >= 0 && data) {
        const char* s = static_cast<const char*>(data);
        if (len > 9 && std::strncmp(s, "PRIORITY=", 9) == 0) s += 9;
        out.priority = std::atoi(s);
    }

    out.message.clear();
    data = nullptr; len = 0;
    if (sd_journal_get_data(journal_, "MESSAGE", &data, &len) >= 0 && data) {
        const char* p = static_cast<const char*>(data);
        if (len > 8 && std::strncmp(p, "MESSAGE=", 8) == 0) { p += 8; len -= 8; }
        out.message = sanitize_utf8(
            std::string_view(p, std::min(len, static_cast<size_t>(1024))));
    }

    // Capture every unit-association field journalctl -u matches on, so per-unit
    // excerpts don't lose entries that only carry UNIT= or SYSLOG_IDENTIFIER=.
    out.unit = read_field("_SYSTEMD_UNIT");
    out.unit_hint = read_field("UNIT");
    out.syslog_id = read_field("SYSLOG_IDENTIFIER");
    return true;
}

// Seek to the tail and pull the last up-to-N entries into the buffer
// (chronological). Leaves the cursor at the tail so subsequent ingest() reads
// only newly-appended entries.
void JournalReader::rebuild_from_tail() {
    if (!journal_) return;
    buffer_.clear();
    if (sd_journal_seek_tail(journal_) < 0) return;

    // Step back up to N entries so the cursor lands ON the first entry we want,
    // then read FORWARD to EOF. previous_skip() lands on an entry rather than
    // past it, so the current entry must be read BEFORE the first next() advance
    // or it is dropped — otherwise a cap of N loads only N-1, and a one-entry
    // journal loads none. Reading forward until next() returns 0 also leaves the
    // journal in the correct follow state so subsequent ingest() picks up
    // appends; anchoring with seek_tail/previous instead leaves next() unable to
    // follow.
    const size_t cap = max_entries_();
    const int moved = sd_journal_previous_skip(journal_, cap);

    const auto deadline = std::chrono::steady_clock::now() + config_.journal_scan_timeout;
    bool have_entry = moved > 0; // cursor is on a valid entry when we stepped back >= 1
    while (have_entry) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        JournalEntry e;
        if (read_current(e)) buffer_.push_back(std::move(e)); // chronological
        if (buffer_.size() > cap) buffer_.pop_front();
        have_entry = sd_journal_next(journal_) > 0;
    }
}

bool JournalReader::open_and_prime() {
    int ret = sd_journal_open(&journal_, SD_JOURNAL_SYSTEM | SD_JOURNAL_CURRENT_USER);
    if (ret < 0 || !journal_) {
        ret = sd_journal_open(&journal_, SD_JOURNAL_LOCAL_ONLY);
        if (ret < 0 || !journal_) {
            journal_ = nullptr;
            return false;
        }
    }
    // Establish the inotify watches up front so ingest()'s sd_journal_wait()
    // reliably detects appends from the very first cycle.
    (void)sd_journal_get_fd(journal_);
    rebuild_from_tail();
    evict();
    return true;
}

void JournalReader::close_handle() {
    if (journal_) sd_journal_close(journal_);
    journal_ = nullptr;
}

bool JournalReader::init() { return open_and_prime(); }

void JournalReader::ingest() {
    if (!journal_) {
        // Degraded — journald may have come up since a failed init(), or a prior
        // API error closed the handle. Retry the open only while degraded; this
        // matches the old per-cycle-open fallback cost, and the healthy path
        // below never re-opens. open_and_prime() refills the buffer, so queries
        // this same cycle already see current data.
        (void)open_and_prime();
        return;
    }
    // Non-blocking wait: sets up the inotify watches on first call and processes
    // pending append/rotate events so sd_journal_next() below sees new entries.
    // (A bare sd_journal_process() does nothing until sd_journal_get_fd()/wait()
    // has established the watches — a common sd_journal follow pitfall.)
    if (sd_journal_wait(journal_, 0) < 0) {
        // A persistent journal error (e.g. journald restarted out from under us).
        // Drop the handle; the degraded branch above re-opens next cycle.
        close_handle();
        return;
    }

    const size_t budget = 2 * max_entries_();
    size_t read = 0;
    while (read < budget) {
        const int r = sd_journal_next(journal_);
        if (r < 0) { close_handle(); return; } // recover on the next cycle
        if (r == 0) break;
        JournalEntry e;
        if (read_current(e)) buffer_.push_back(std::move(e));
        ++read;
    }
    if (read >= budget) {
        // Fell far behind (a burst larger than the buffer). Jump to a recent
        // window instead of draining the whole backlog — bounds per-cycle work.
        rebuild_from_tail();
    }
    evict();
}

#else // no systemd

bool JournalReader::read_current(JournalEntry&) const { return false; }
void JournalReader::rebuild_from_tail() {}
bool JournalReader::init() { return false; }
void JournalReader::ingest() {}

#endif

// --- Members delegate to the testable journal_detail buffer operations -------

void JournalReader::evict() { journal_detail::evict(config_, buffer_); }

uint32_t JournalReader::error_count() const {
    return journal_detail::error_count(config_, buffer_);
}

Severity JournalReader::overall_severity() const {
    return journal_detail::overall_severity(config_, buffer_);
}

std::vector<std::string> JournalReader::recent_errors() const {
    return journal_detail::recent_errors(config_, buffer_);
}

std::vector<std::string> JournalReader::excerpt_for_unit(const std::string& unit) const {
    return journal_detail::excerpt_for_unit(config_, buffer_, unit);
}

} // namespace edge
