// SPDX-License-Identifier: MIT
// edge-healthd: serialization-safety regression tests
//
// These guard against a crash-loop bug: journal/unit log text is arbitrary
// bytes and can be non-UTF-8, which makes a strict nlohmann::json::dump() throw
// type_error(316). Uncaught, one bad byte in the journal tail terminates the
// daemon and re-terminates it on every restart. Separately, NaN/Inf doubles
// serialize to JSON `null`, which is invalid against the schema.

#include <catch2/catch_test_macros.hpp>

#include "journal.hpp"
#include "json.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace edge;

TEST_CASE("sanitize_utf8 replaces invalid bytes and preserves valid text", "[json][utf8]") {
    // Pure ASCII passes through untouched.
    CHECK(sanitize_utf8("hello world") == "hello world");

    // Valid multibyte UTF-8 (U+00E9 é, U+20AC €) is preserved verbatim.
    const std::string valid = "caf\xC3\xA9 \xE2\x82\xAC";
    CHECK(sanitize_utf8(valid) == valid);

    // Lone continuation byte and a raw 0xFF/0xFE become U+FFFD, and the result
    // is itself well-formed UTF-8.
    const std::string bad = std::string("a\xFF\xFE\x80""b");
    const std::string cleaned = sanitize_utf8(bad);
    CHECK(cleaned.find('\xFF') == std::string::npos);
    CHECK(cleaned.find('\xFE') == std::string::npos);
    CHECK(cleaned.find('\x80') == std::string::npos);
    // Re-sanitizing a sanitized string is a no-op (idempotent / valid UTF-8).
    CHECK(sanitize_utf8(cleaned) == cleaned);

    // Truncated multibyte lead byte at end of input is replaced, not read OOB.
    CHECK_NOTHROW(sanitize_utf8(std::string("x\xE2\x82")));
}

TEST_CASE("serialize does not throw on invalid UTF-8 in journal errors", "[json][utf8]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    // Raw non-UTF-8 bytes as they might arrive from the journal MESSAGE field.
    state.journal.recent_errors = {
        std::string("bad byte \xFF\xFE here"),
        std::string("lone continuation \x80\x81"),
    };

    std::string out;
    REQUIRE_NOTHROW(out = json::serialize(state));
    CHECK_FALSE(out.empty());
    // Output must itself be parseable valid JSON.
    REQUIRE_NOTHROW(nlohmann::json::parse(out));

    // Pretty variant must be equally safe.
    REQUIRE_NOTHROW(json::serialize_pretty(state));
}

TEST_CASE("serialize does not throw on invalid UTF-8 in service log fields", "[json][utf8]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();

    ServiceUnit unit;
    unit.name = "broken.service";
    unit.state = ServiceState::Failed;
    unit.severity = Severity::Crit;
    unit.detail = std::string("detail with \xFF invalid");
    unit.log_excerpt = {std::string("log line \xC0\xC1 overlong-ish")};
    state.services.units.push_back(unit);

    std::string out;
    REQUIRE_NOTHROW(out = json::serialize(state));
    CHECK_FALSE(out.empty());

    auto parsed = nlohmann::json::parse(out);
    CHECK(parsed["services"]["units"][0]["name"] == "broken.service");
}

TEST_CASE("serialize coerces NaN/Inf doubles to valid numbers", "[json][nan]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.resources.cpu.load1 = std::nan("");
    state.resources.cpu.load5 = std::numeric_limits<double>::infinity();
    state.resources.cpu.load15 = -std::numeric_limits<double>::infinity();

    ThermalSensor sensor;
    sensor.sensor = "cpu-thermal";
    sensor.temp_c = std::numeric_limits<double>::infinity();
    state.resources.thermal.push_back(sensor);

    std::string out;
    REQUIRE_NOTHROW(out = json::serialize(state));

    // No JSON null / nan / inf tokens should appear anywhere in the output.
    CHECK(out.find("null") == std::string::npos);
    CHECK(out.find("nan") == std::string::npos);
    CHECK(out.find("inf") == std::string::npos);

    auto parsed = nlohmann::json::parse(out);
    // The affected fields must parse back as JSON numbers, not null.
    CHECK(parsed["resources"]["cpu"]["load1"].is_number());
    CHECK(parsed["resources"]["cpu"]["load5"].is_number());
    CHECK(parsed["resources"]["cpu"]["load15"].is_number());
    CHECK(parsed["resources"]["thermal"][0]["temp_c"].is_number());
}
