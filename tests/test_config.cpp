// SPDX-License-Identifier: MIT
// edge-healthd: Configuration tests

#include <catch2/catch_test_macros.hpp>
#include "config.hpp"

#include <filesystem>
#include <fstream>

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

TEST_CASE("Config update_check_interval defaults to 1800s", "[config]") {
    auto config = Config::defaults();

    CHECK(config.update_check_interval.count() == 1800);
}

TEST_CASE("Config to_json_string includes update_check_interval_sec", "[config]") {
    auto config = Config::defaults();

    CHECK(config.to_json_string().find("\"update_check_interval_sec\"") != std::string::npos);
}

TEST_CASE("Config validate rejects update_check_interval_sec < 1", "[config]") {
    auto config = Config::defaults();
    config.update_check_interval = std::chrono::seconds{0};

    auto error = config.validate();
    REQUIRE(error.has_value());
    CHECK(error->find("update_check_interval") != std::string::npos);
}

TEST_CASE("Config RTC defaults", "[config][rtc]") {
    auto config = Config::defaults();

    CHECK(config.enable_rtc == true);
    CHECK(config.rtc_device == "/sys/class/rtc/rtc0");
    CHECK(config.thresholds.rtc_voltage_warn_mv == 2700);
    CHECK(config.thresholds.rtc_voltage_crit_mv == 2500);
}

TEST_CASE("Config to_json_string includes RTC fields", "[config][rtc]") {
    auto config = Config::defaults();
    auto json_str = config.to_json_string();

    CHECK(json_str.find("\"enable_rtc\"") != std::string::npos);
    CHECK(json_str.find("\"rtc_device\"") != std::string::npos);
    CHECK(json_str.find("\"rtc_voltage_warn_mv\"") != std::string::npos);
    CHECK(json_str.find("\"rtc_voltage_crit_mv\"") != std::string::npos);
}

TEST_CASE("Config RTC device path is configurable", "[config][rtc]") {
    auto config = Config::defaults();
    config.rtc_device = "/sys/class/rtc/rtc1";

    CHECK(config.rtc_device == "/sys/class/rtc/rtc1");
    CHECK(config.to_json_string().find("rtc1") != std::string::npos);
}

TEST_CASE("Config trigger_min_interval defaults to 5s", "[config][trigger]") {
    auto config = Config::defaults();
    CHECK(config.trigger_min_interval.count() == 5);
}

TEST_CASE("Config trigger_min_interval_sec is parsed from JSON", "[config][trigger]") {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "trigger_interval_test.conf";
    std::ofstream f(tmp);
    f << R"({"trigger_min_interval_sec": 10})";
    f.close();

    auto config = Config::load(tmp);
    CHECK(config.trigger_min_interval.count() == 10);
    fs::remove(tmp);
}

TEST_CASE("Config trigger_min_interval_sec=0 disables rate-limit", "[config][trigger]") {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "trigger_zero_test.conf";
    std::ofstream f(tmp);
    f << R"({"trigger_min_interval_sec": 0})";
    f.close();

    auto config = Config::load(tmp);
    CHECK(config.trigger_min_interval.count() == 0);
    CHECK(!config.validate().has_value());  // 0 is valid (disables rate-limit)
    fs::remove(tmp);
}

TEST_CASE("Config to_json_string includes trigger_min_interval_sec", "[config][trigger]") {
    auto config = Config::defaults();
    CHECK(config.to_json_string().find("\"trigger_min_interval_sec\"") != std::string::npos);
}
