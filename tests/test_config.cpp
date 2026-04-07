// SPDX-License-Identifier: MIT
// edge-healthd: Configuration tests

#include <catch2/catch_test_macros.hpp>
#include "config.hpp"

using namespace edge;

TEST_CASE("Config defaults are valid", "[config]") {
    auto config = Config::defaults();

    CHECK(config.collect_interval.count() == 60);
    CHECK(config.sample_window_sec == 60);
    CHECK(config.enable_ntp == true);
    CHECK(config.enable_thermal == true);
    CHECK(config.log_level == "info");
    CHECK(config.dbus_timeout.count() == 2000);

    // Validate should pass for defaults
    auto error = config.validate();
    CHECK_FALSE(error.has_value());
}

TEST_CASE("Config threshold defaults", "[config]") {
    auto config = Config::defaults();

    CHECK(config.thresholds.cpu_load_warn == 80);
    CHECK(config.thresholds.cpu_load_crit == 95);
    CHECK(config.thresholds.mem_used_warn == 80);
    CHECK(config.thresholds.mem_used_crit == 95);
    CHECK(config.thresholds.disk_used_warn == 80);
    CHECK(config.thresholds.disk_used_crit == 95);
    CHECK(config.thresholds.temp_warn_c == 70.0);
    CHECK(config.thresholds.temp_crit_c == 85.0);
}

TEST_CASE("Config validation catches invalid thresholds", "[config]") {
    auto config = Config::defaults();

    // Invalid: warn >= crit
    config.thresholds.cpu_load_warn = 95;
    config.thresholds.cpu_load_crit = 80;

    auto error = config.validate();
    REQUIRE(error.has_value());
    CHECK(error->find("cpu_load") != std::string::npos);
}

TEST_CASE("Config validation catches invalid interval", "[config]") {
    auto config = Config::defaults();

    config.collect_interval = std::chrono::seconds(0);

    auto error = config.validate();
    REQUIRE(error.has_value());
    CHECK(error->find("interval") != std::string::npos);
}

TEST_CASE("Config default paths", "[config]") {
    auto config = Config::defaults();

    CHECK(config.snapshot_file == "/run/health/state.json");
    CHECK(config.state_dir == "/data/edge/health");
    CHECK(config.config_file == "/etc/edge/healthd.conf");
}

TEST_CASE("Config default monitored items", "[config]") {
    auto config = Config::defaults();

    CHECK(config.monitored_services.size() == 2);
    CHECK(config.monitored_mounts.size() == 2);
    CHECK(config.monitored_interfaces.size() == 2);
}

TEST_CASE("Config time_sync_interval defaults to 300s", "[config]") {
    auto config = Config::defaults();

    CHECK(config.time_sync_interval.count() == 300);
}

TEST_CASE("Config enable_ptp defaults to false", "[config]") {
    auto config = Config::defaults();

    CHECK(config.enable_ptp == false);
}

TEST_CASE("Config PTP threshold defaults", "[config]") {
    auto config = Config::defaults();

    CHECK(config.thresholds.ptp_offset_warn_ns == 10000);
    CHECK(config.thresholds.ptp_offset_crit_ns == 100000);
}

TEST_CASE("Config validate rejects time_sync_interval_sec < 1", "[config]") {
    auto config = Config::defaults();

    config.time_sync_interval = std::chrono::seconds(0);

    auto error = config.validate();
    REQUIRE(error.has_value());
    CHECK(error->find("time_sync_interval") != std::string::npos);
}

TEST_CASE("Config to_json_string includes time_sync_interval and enable_ptp", "[config]") {
    auto config = Config::defaults();
    auto json_str = config.to_json_string();

    CHECK(json_str.find("\"time_sync_interval_sec\"") != std::string::npos);
    CHECK(json_str.find("\"enable_ptp\"") != std::string::npos);
    CHECK(json_str.find("\"ptp_offset_warn_ns\"") != std::string::npos);
    CHECK(json_str.find("\"ptp_offset_crit_ns\"") != std::string::npos);
}
