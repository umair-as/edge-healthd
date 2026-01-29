// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "journal.hpp"

using namespace edge;

static JournalEntry e(uint64_t usec, int pri, const std::string& m) {
    JournalEntry je; je.realtime_usec = usec; je.priority = pri; je.message = m; return je;
}

TEST_CASE("filter_journal_entries respects max_lines and priority and window", "[journal]") {
    Config cfg = Config::defaults();
    cfg.log_excerpt_max_lines = 3;
    cfg.log_excerpt_min_priority = 4; // warning and above (0..4)
    cfg.log_excerpt_window_sec = std::nullopt; // no window

    std::vector<JournalEntry> entries;
    // newest first
    entries.push_back(e(3000, 6, "info low"));
    entries.push_back(e(2000, 4, "warn ok"));
    entries.push_back(e(1000, 2, "crit important"));
    entries.push_back(e(500, 3, "err past"));

    auto out = filter_journal_entries(cfg, entries);
    // Should include only priority <=4 entries (warn, crit, err), up to 3 lines
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == "warn ok");
    REQUIRE(out[1] == "crit important");
    REQUIRE(out[2] == "err past");
}

TEST_CASE("filter_journal_entries applies time window", "[journal]") {
    Config cfg = Config::defaults();
    cfg.log_excerpt_max_lines = 10;
    cfg.log_excerpt_min_priority = std::nullopt;
    cfg.log_excerpt_window_sec = 2; // include only entries with realtime_usec >= now-2s

    // Create entries with timestamps relative to now: we'll compute now_us and set accordingly
    auto now = std::chrono::system_clock::now();
    uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::vector<JournalEntry> entries;
    // newest first: now, now-1s, now-3s
    entries.push_back(e(now_us, 6, "now"));
    entries.push_back(e(now_us - 1000000ULL, 6, "one_sec"));
    entries.push_back(e(now_us - 3000000ULL, 6, "three_sec"));

    auto out = filter_journal_entries(cfg, entries);
    // should include 'now' and 'one_sec' (newest first) but not 'three_sec'
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == "now");
    REQUIRE(out[1] == "one_sec");
}
