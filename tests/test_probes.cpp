// SPDX-License-Identifier: MIT
// edge-healthd: Probe tests

#include <catch2/catch_test_macros.hpp>
#include "probes.hpp"
#include "config.hpp"
#include "netlink_monitor.hpp"

using namespace edge;

TEST_CASE("Probe concept compliance", "[probes]") {
    // These are compile-time checks via static_assert in probes.hpp
    // If this file compiles, the concepts are satisfied
    CHECK(true);
}

TEST_CASE("DeviceProbe collects device info", "[probes]") {
    auto config = Config::defaults();
    DeviceProbe probe(config);

    auto result = probe.collect();
    REQUIRE(result.has_value());

    auto& device = *result;
    // Should have hostname (unless in weird environment)
    CHECK_FALSE(device.hostname.empty());
    // Should detect architecture
    CHECK_FALSE(device.arch.empty());
}

TEST_CASE("BootProbe reads uptime", "[probes]") {
    auto config = Config::defaults();
    BootProbe probe(config, "/tmp/edge-snapshot-test");

    auto result = probe.collect();
    REQUIRE(result.has_value());

    auto& boot = *result;
    // Uptime should be positive
    CHECK(boot.uptime.count() > 0);
    // Boot ID should not be empty
    CHECK_FALSE(boot.boot_id.empty());
}

TEST_CASE("ResourcesProbe reads system info", "[probes]") {
    auto config = Config::defaults();
    config.monitored_mounts = {"/"};
    config.monitored_interfaces = {};

    NetlinkMonitor nl_monitor;
    ResourcesProbe probe(config, nl_monitor, config.monitored_mounts, config.monitored_interfaces);

    auto result = probe.collect();
    REQUIRE(result.has_value());

    auto& resources = *result;

    // CPU load should be non-negative
    CHECK(resources.cpu.load1 >= 0.0);
    CHECK(resources.cpu.load5 >= 0.0);
    CHECK(resources.cpu.load15 >= 0.0);

    // Memory should be detected
    CHECK(resources.memory.mem_total_mb > 0);

    // Root mount should be present
    REQUIRE(resources.storage.size() >= 1);
    CHECK(resources.storage[0].mount == "/");
}

TEST_CASE("ProbeError formatting", "[probes]") {
    ProbeError err{
        .probe = "test",
        .message = "something went wrong",
        .code = 42
    };

    auto what = err.what();
    CHECK(what.find("test") != std::string::npos);
    CHECK(what.find("something went wrong") != std::string::npos);
    CHECK(what.find("42") != std::string::npos);
}

TEST_CASE("make_error helper", "[probes]") {
    auto err = make_error("device", "failed to read", 5);

    CHECK(err.probe == "device");
    CHECK(err.message == "failed to read");
    CHECK(err.code == 5);
}

TEST_CASE("JournalProbe collects system journal", "[probes]") {
    auto config = Config::defaults();
    JournalProbe probe(config);

    auto result = probe.collect();
    REQUIRE(result.has_value());

    auto& journal = *result;
    // Overall should be a known severity (not indeterminate)
    CHECK((journal.overall == Severity::Ok ||
           journal.overall == Severity::Warn ||
           journal.overall == Severity::Crit ||
           journal.overall == Severity::Unknown));
    // error_count must be consistent with recent_errors (count >= recent_errors.size())
    CHECK(journal.error_count >= journal.recent_errors.size());
}
