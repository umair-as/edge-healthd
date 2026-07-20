// SPDX-License-Identifier: MIT
// Tests for the JournalReader buffer operations (journal_detail::*). These
// exercise severity/error-count/window/per-unit/eviction logic against a
// synthetic buffer, with no dependency on a live sd_journal (the persistent
// handle + rotation-following are covered by the on-device gate).

#include <catch2/catch_test_macros.hpp>

#include "config.hpp"
#include "journal_reader.hpp"

#include <chrono>
#include <deque>

using namespace edge;
using edge::journal_detail::error_count;
using edge::journal_detail::evict;
using edge::journal_detail::excerpt_for_unit;
using edge::journal_detail::overall_severity;
using edge::journal_detail::recent_errors;

namespace {
uint64_t now_usec() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
// entry `sec_ago` seconds in the past
JournalEntry entry(int priority, const std::string& msg, const std::string& unit,
                   uint64_t sec_ago = 0) {
    return JournalEntry{now_usec() - sec_ago * 1000000ULL, priority, msg, unit};
}
} // namespace

TEST_CASE("JournalReader: overall_severity from buffer", "[journal_reader]") {
    Config cfg = Config::defaults();

    SECTION("no error entries -> Ok") {
        std::deque<JournalEntry> b{entry(6, "info", "a.service"),
                                   entry(5, "notice", "b.service")};
        CHECK(overall_severity(cfg, b) == Severity::Ok);
        CHECK(error_count(cfg, b) == 0);
    }
    SECTION("an err (3) entry -> Warn") {
        std::deque<JournalEntry> b{entry(6, "info", "a.service"),
                                   entry(3, "oops", "a.service")};
        CHECK(overall_severity(cfg, b) == Severity::Warn);
        CHECK(error_count(cfg, b) == 1);
    }
    SECTION("a crit (2) entry -> Crit, dominates warn") {
        std::deque<JournalEntry> b{entry(3, "warn", "a.service"),
                                   entry(2, "crit", "a.service")};
        CHECK(overall_severity(cfg, b) == Severity::Crit);
        CHECK(error_count(cfg, b) == 2); // both <= err
    }
}

TEST_CASE("JournalReader: window filters error_count/severity", "[journal_reader]") {
    Config cfg = Config::defaults();
    cfg.log_excerpt_window_sec = 60; // only last 60s counts

    std::deque<JournalEntry> b{
        entry(2, "old crit", "a.service", 600), // 10 min ago -> excluded
        entry(6, "recent info", "a.service", 5),
    };
    CHECK(error_count(cfg, b) == 0);              // old crit outside window
    CHECK(overall_severity(cfg, b) == Severity::Ok);

    b.push_back(entry(3, "recent err", "a.service", 5));
    CHECK(error_count(cfg, b) == 1);
    CHECK(overall_severity(cfg, b) == Severity::Warn);
}

TEST_CASE("JournalReader: excerpt_for_unit filters by unit, all priorities", "[journal_reader]") {
    Config cfg = Config::defaults();
    // chronological (oldest first); excerpt returns newest-first
    std::deque<JournalEntry> b{
        entry(6, "a-first", "a.service", 3),
        entry(6, "b-only", "b.service", 2),
        entry(5, "a-second", "a.service", 1),
    };
    auto a = excerpt_for_unit(cfg, b, "a.service");
    REQUIRE(a.size() == 2);
    CHECK(a[0] == "a-second"); // newest-first
    CHECK(a[1] == "a-first");

    auto b_unit = excerpt_for_unit(cfg, b, "b.service");
    REQUIRE(b_unit.size() == 1);
    CHECK(b_unit[0] == "b-only");

    CHECK(excerpt_for_unit(cfg, b, "missing.service").empty()); // aged-out/unknown -> empty
}

TEST_CASE("JournalReader: recent_errors returns only error lines, newest-first", "[journal_reader]") {
    Config cfg = Config::defaults();
    std::deque<JournalEntry> b{
        entry(6, "info line", "a.service", 3),
        entry(3, "err one", "a.service", 2),
        entry(2, "crit two", "b.service", 1),
    };
    auto errs = recent_errors(cfg, b);
    REQUIRE(errs.size() == 2);        // info excluded
    CHECK(errs[0] == "crit two");     // newest-first
    CHECK(errs[1] == "err one");
}

TEST_CASE("JournalReader: evict bounds the buffer by count and age", "[journal_reader]") {
    SECTION("count cap drops oldest") {
        Config cfg = Config::defaults();
        cfg.journal_buffer_max_entries = 3;
        std::deque<JournalEntry> b;
        for (int i = 0; i < 6; ++i) b.push_back(entry(6, "m" + std::to_string(i), "a", 0));
        evict(cfg, b);
        CHECK(b.size() == 3);
        CHECK(b.front().message == "m3"); // oldest three dropped
    }
    SECTION("count-only: old entries are retained when under the cap") {
        // Eviction is count-bound only (no age bound), so a quiet unit's older
        // logs stay available for its excerpt.
        Config cfg = Config::defaults();
        cfg.journal_buffer_max_entries = 1000;
        std::deque<JournalEntry> b{
            entry(6, "ancient", "a", 100000), // ~27h old, still kept (under cap)
            entry(6, "fresh", "a", 10),
        };
        evict(cfg, b);
        REQUIRE(b.size() == 2);
        CHECK(b.front().message == "ancient");
    }
}
