// SPDX-License-Identifier: MIT
// edge-healthd: Probe tests

#include <catch2/catch_test_macros.hpp>
#include "probes.hpp"
#include "config.hpp"
#include "netlink_monitor.hpp"

#include <filesystem>
#include <fstream>

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

TEST_CASE("CrashProbe detects and deduplicates pstore artifacts", "[probes]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crashprobe-test");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    {
        std::ofstream file(pstore_dir / "dmesg-erst-0");
        REQUIRE(file.good());
        file << "Kernel panic - not syncing";
    }

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto first = probe.collect();
    REQUIRE(first.has_value());
    CHECK(first->present == true);
    CHECK(first->source.has_value());
    CHECK(*first->source == "pstore");
    CHECK(first->artifact_count == 1);
    CHECK(first->fingerprint.has_value());
    CHECK(first->acknowledged == false);

    // Probe must not auto-write the state file — acknowledgement is an
    // external action (D-Bus method, operator tool). Verify nothing was
    // written by the probe itself.
    CHECK_FALSE(std::filesystem::exists(state_dir / "crash_state.json"));

    // Simulate an external acknowledgement and re-collect.
    {
        std::ofstream out(state_dir / "crash_state.json");
        REQUIRE(out.good());
        out << "{\"acknowledged_fingerprint\":\"" << *first->fingerprint << "\"}";
    }

    auto second = probe.collect();
    REQUIRE(second.has_value());
    CHECK(second->present == true);
    CHECK(second->fingerprint == first->fingerprint);
    CHECK(second->acknowledged == true);

    std::filesystem::remove_all(base, ec);
}
