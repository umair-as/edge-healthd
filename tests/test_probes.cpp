// SPDX-License-Identifier: MIT
// edge-healthd: Probe tests

#include <catch2/catch_test_macros.hpp>
#include "probes.hpp"
#include "aggregator.hpp"
#include "atomic_file.hpp"
#include "config.hpp"
#include "netlink_monitor.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

using namespace edge;

namespace {

// Current boot's wall-clock start, mirroring CrashProbe's internal computation
// (system now minus CLOCK_BOOTTIME uptime). Tests set artifact mtimes relative
// to this to exercise the current-boot vs prior-boot aging logic.
std::chrono::system_clock::time_point test_boot_start() {
    auto start = std::chrono::system_clock::now();
    if (timespec ts{}; clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
        start -= std::chrono::seconds(static_cast<int64_t>(ts.tv_sec));
    }
    return start;
}

// Set a file's mtime to `when` (system_clock), translating into file_clock.
void set_file_mtime(const std::filesystem::path& p,
                    std::chrono::system_clock::time_point when) {
    using namespace std::chrono;
    const auto delta = when - system_clock::now();
    const auto ftime = std::filesystem::file_time_type::clock::now() +
                       duration_cast<std::filesystem::file_time_type::duration>(delta);
    std::error_code ec;
    std::filesystem::last_write_time(p, ftime, ec);
}

std::filesystem::path write_pstore_file(const std::filesystem::path& dir,
                                        const std::string& name,
                                        const std::string& body) {
    const auto path = dir / name;
    std::ofstream file(path);
    file << body;
    file.close();
    return path;
}

} // namespace

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

TEST_CASE("CrashProbe: console-ramoops only is not a panic", "[probes][crash]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crash-console");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    // console-ramoops is the ordinary boot console log, archived every boot —
    // NOT a kernel fault. It must never drive crit or produce a fingerprint.
    write_pstore_file(pstore_dir, "console-ramoops-0", "normal boot console output");

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto r = probe.collect();
    REQUIRE(r.has_value());
    CHECK(r->present == true);          // artifact exists (visibility)
    CHECK(r->panic_count == 0);         // but no kernel-fault dump
    CHECK_FALSE(r->fingerprint.has_value());
    CHECK(r->artifacts.size() == 1);
    CHECK(r->artifacts[0].kind == "informational");

    SnapshotAggregator agg(config);
    CHECK(agg.evaluate_crash(*r) == Severity::Ok);

    std::filesystem::remove_all(base, ec);
}

TEST_CASE("CrashProbe: prior-boot panic is historical (warn)", "[probes][crash]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crash-prior");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    auto path = write_pstore_file(pstore_dir, "dmesg-ramoops-0", "Oops from an older kernel");
    // Age the dump to an hour before this boot started → prior boot.
    set_file_mtime(path, test_boot_start() - std::chrono::hours(1));

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto r = probe.collect();
    REQUIRE(r.has_value());
    CHECK(r->panic_count == 1);
    CHECK(r->artifacts[0].kind == "panic");
    CHECK_FALSE(r->panic_current_boot);
    CHECK_FALSE(r->acknowledged);

    SnapshotAggregator agg(config);
    CHECK(agg.evaluate_crash(*r) == Severity::Warn);

    std::filesystem::remove_all(base, ec);
}

TEST_CASE("CrashProbe: current-boot panic unacked is crit", "[probes][crash]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crash-current");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    auto path = write_pstore_file(pstore_dir, "dmesg-ramoops-0", "Kernel panic - not syncing");
    set_file_mtime(path, std::chrono::system_clock::now());  // captured this boot

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto r = probe.collect();
    REQUIRE(r.has_value());
    CHECK(r->panic_count == 1);
    CHECK(r->panic_current_boot);
    CHECK_FALSE(r->acknowledged);

    SnapshotAggregator agg(config);
    CHECK(agg.evaluate_crash(*r) == Severity::Crit);

    std::filesystem::remove_all(base, ec);
}

TEST_CASE("CrashProbe: acknowledged current-boot panic is not crit", "[probes][crash]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crash-ack");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    auto path = write_pstore_file(pstore_dir, "dmesg-ramoops-0", "Kernel panic - not syncing");
    set_file_mtime(path, std::chrono::system_clock::now());

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto first = probe.collect();
    REQUIRE(first.has_value());
    REQUIRE(first->fingerprint.has_value());
    SnapshotAggregator agg(config);
    CHECK(agg.evaluate_crash(*first) == Severity::Crit);  // fresh, unacked

    // Simulate AcknowledgeCrash: persist the panic fingerprint atomically, the
    // same way SnapshotDaemon's on_acknowledge_ callback does.
    nlohmann::json j;
    j["acknowledged_fingerprint"] = *first->fingerprint;
    auto w = atomic_write_file(state_dir / "crash_state.json", j.dump(2));
    REQUIRE(w.has_value());

    auto second = probe.collect();
    REQUIRE(second.has_value());
    CHECK(second->acknowledged == true);
    CHECK(agg.evaluate_crash(*second) == Severity::Ok);  // acked → informational

    std::filesystem::remove_all(base, ec);
}

TEST_CASE("CrashProbe: fingerprint stable when only console artifact changes",
          "[probes][crash]") {
    const auto base = std::filesystem::path("/tmp/edge-healthd-crash-stable");
    const auto state_dir = base / "state";
    const auto pstore_dir = base / "pstore";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(state_dir, ec);
    std::filesystem::create_directories(pstore_dir, ec);

    write_pstore_file(pstore_dir, "dmesg-ramoops-0", "Kernel panic - not syncing");
    write_pstore_file(pstore_dir, "console-ramoops-0", "boot log rev 1");

    auto config = Config::defaults();
    CrashProbe probe(config, state_dir, pstore_dir);

    auto first = probe.collect();
    REQUIRE(first.has_value());
    REQUIRE(first->fingerprint.has_value());

    // Rewrite only the benign console artifact (as happens every boot). The
    // panic-only fingerprint must not churn.
    write_pstore_file(pstore_dir, "console-ramoops-0", "boot log rev 2 — different size!!");

    auto second = probe.collect();
    REQUIRE(second.has_value());
    REQUIRE(second->fingerprint.has_value());
    CHECK(second->fingerprint == first->fingerprint);

    std::filesystem::remove_all(base, ec);
}
