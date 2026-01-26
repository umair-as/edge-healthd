// SPDX-License-Identifier: MIT
// edge-healthd: Aggregator tests

#include <catch2/catch_test_macros.hpp>
#include "aggregator.hpp"

using namespace edge;

TEST_CASE("Aggregator evaluates boot severity", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    SECTION("No failures is OK") {
        BootStatus boot;
        boot.boot_ok = true;
        boot.boot_fail_count = 0;

        CHECK(agg.evaluate_boot(boot) == Severity::Ok);
    }

    SECTION("One failure is warn") {
        BootStatus boot;
        boot.boot_ok = false;
        boot.boot_fail_count = 1;

        CHECK(agg.evaluate_boot(boot) == Severity::Warn);
    }

    SECTION("Many failures is crit") {
        BootStatus boot;
        boot.boot_ok = false;
        boot.boot_fail_count = 5;

        CHECK(agg.evaluate_boot(boot) == Severity::Crit);
    }
}

TEST_CASE("Aggregator evaluates memory severity", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    ResourcesStatus resources;
    resources.memory.mem_total_mb = 1000;

    SECTION("Low usage is OK") {
        resources.memory.mem_used_mb = 500; // 50%
        CHECK(agg.evaluate_resources(resources) == Severity::Ok);
    }

    SECTION("High usage is warn") {
        resources.memory.mem_used_mb = 850; // 85%
        CHECK(agg.evaluate_resources(resources) == Severity::Warn);
    }

    SECTION("Critical usage") {
        resources.memory.mem_used_mb = 960; // 96%
        CHECK(agg.evaluate_resources(resources) == Severity::Crit);
    }
}

TEST_CASE("Aggregator computes overall severity", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    SECTION("All OK means overall OK") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Ok);
    }

    SECTION("Any crit means overall crit") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Crit, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Crit);
    }

    SECTION("Warn without crit means warn") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Warn, Severity::Ok, Severity::Warn, Severity::Ok);
        CHECK(overall == Severity::Warn);
    }

    SECTION("Unknown does not override ok") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Unknown, Severity::Ok, Severity::Unknown, Severity::Ok);
        CHECK(overall == Severity::Ok);
    }
}

TEST_CASE("Aggregator generates reasons", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    DeviceInfo device;
    BootStatus boot;
    boot.boot_fail_count = 2;

    ServicesStatus services;
    ServiceUnit failed_unit;
    failed_unit.name = "test.service";
    failed_unit.state = ServiceState::Failed;
    services.units.push_back(failed_unit);

    ResourcesStatus resources;
    TimeSyncStatus time_sync;
    UpdateStatus update;

    auto state = agg.aggregate(device, boot, services, resources, time_sync, update);

    CHECK_FALSE(state.summary.reasons.empty());
    // Should include reason codes for boot failures and failed service
    bool has_boot_reason = false;
    bool has_service_reason = false;
    for (const auto& reason : state.summary.reasons) {
        if (reason == "boot_failures") {
            has_boot_reason = true;
        }
        if (reason == "svc_failed:test.service") {
            has_service_reason = true;
        }
    }
    CHECK(has_boot_reason);
    CHECK(has_service_reason);
}
