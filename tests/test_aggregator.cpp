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

TEST_CASE("Aggregator: unreadable storage/thermal is unavailable, not ok", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    ResourcesStatus resources;
    resources.memory.mem_total_mb = 1000;
    resources.memory.mem_used_mb = 100; // healthy, so it doesn't mask the test

    SECTION("Unmounted/unreadable mount -> Unavailable (R2), not Ok") {
        StorageMount m;
        m.mount = "/data";
        m.available = false; // statvfs failed
        resources.storage.push_back(m);
        CHECK(agg.evaluate_resources(resources) == Severity::Unavailable);
    }

    SECTION("Available mount at healthy usage stays Ok") {
        StorageMount m;
        m.mount = "/";
        m.available = true;
        m.used_pct = uint8_t{10};
        m.avail_mb = uint64_t{5000};
        resources.storage.push_back(m);
        CHECK(agg.evaluate_resources(resources) == Severity::Ok);
    }

    SECTION("A real crit mount dominates an unavailable one") {
        StorageMount bad;
        bad.mount = "/data";
        bad.available = false;
        StorageMount full;
        full.mount = "/";
        full.available = true;
        full.used_pct = uint8_t{99}; // crit
        resources.storage.push_back(bad);
        resources.storage.push_back(full);
        CHECK(agg.evaluate_resources(resources) == Severity::Crit);
    }

    SECTION("Dead thermal sensor -> Unavailable (R3), not Ok") {
        ThermalSensor s;
        s.sensor = "cpu-thermal";
        s.available = false; // temp read failed
        resources.thermal.push_back(s);
        CHECK(agg.evaluate_resources(resources) == Severity::Unavailable);
    }
}

TEST_CASE("Aggregator computes overall severity", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    SECTION("All OK means overall OK") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Ok);
    }

    SECTION("Any crit means overall crit") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Crit, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Crit);
    }

    SECTION("Warn without crit means warn") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Warn, Severity::Ok, Severity::Warn, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Warn);
    }

    SECTION("Unknown does not override ok") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Unknown, Severity::Ok, Severity::Unknown, Severity::Ok, Severity::Ok, Severity::Ok);
        CHECK(overall == Severity::Ok);
    }

    SECTION("Journal crit propagates to overall") {
        auto overall = agg.compute_overall(
            Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Ok, Severity::Crit, Severity::Ok);
        CHECK(overall == Severity::Crit);
    }
}

TEST_CASE("Aggregator evaluates journal severity", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    SECTION("Unknown overall returns Unknown") {
        JournalStatus journal;
        journal.overall = Severity::Unknown;
        CHECK(agg.evaluate_journal(journal) == Severity::Unknown);
    }

    SECTION("Ok overall returns Ok") {
        JournalStatus journal;
        journal.overall = Severity::Ok;
        CHECK(agg.evaluate_journal(journal) == Severity::Ok);
    }

    SECTION("Warn overall returns Warn") {
        JournalStatus journal;
        journal.overall = Severity::Warn;
        journal.error_count = 3;
        CHECK(agg.evaluate_journal(journal) == Severity::Warn);
    }

    SECTION("Crit overall returns Crit") {
        JournalStatus journal;
        journal.overall = Severity::Crit;
        journal.error_count = 1;
        CHECK(agg.evaluate_journal(journal) == Severity::Crit);
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
    JournalStatus journal;
    CrashStatus crash;

    auto state = agg.aggregate(device, boot, services, resources, time_sync, update, journal, crash);

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

TEST_CASE("Aggregator adds crash reasons for current-boot panic", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    DeviceInfo device;
    BootStatus boot;
    ServicesStatus services;
    ResourcesStatus resources;
    TimeSyncStatus time_sync;
    UpdateStatus update;
    JournalStatus journal;
    CrashStatus crash;
    crash.present = true;
    crash.acknowledged = false;
    crash.panic_count = 1;
    crash.panic_current_boot = true;  // fresh kernel fault this boot → crit

    auto state = agg.aggregate(device, boot, services, resources, time_sync, update, journal, crash);

    bool has_kernel_panic = false;
    bool has_pstore_present = false;
    for (const auto& reason : state.summary.reasons) {
        if (reason == "kernel_panic_detected") {
            has_kernel_panic = true;
        }
        if (reason == "pstore_records_present") {
            has_pstore_present = true;
        }
    }
    CHECK(has_kernel_panic);
    CHECK(has_pstore_present);
    CHECK(state.summary.severity == Severity::Crit);

    // Per-domain severity surfaces the crash domain without masking the others.
    CHECK(state.summary.domains.crash == Severity::Crit);
    CHECK(state.summary.domains.boot == Severity::Ok);
    CHECK(state.summary.domains.resources == Severity::Ok);
}

TEST_CASE("Aggregator: prior-boot panic is historical, not crit", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    CrashStatus crash;
    crash.present = true;
    crash.acknowledged = false;
    crash.panic_count = 1;
    crash.panic_current_boot = false;  // only prior-boot dumps → warn

    auto state = agg.aggregate({}, {}, {}, {}, {}, {}, {}, crash);

    bool has_kernel_panic = false;
    bool has_historical = false;
    for (const auto& reason : state.summary.reasons) {
        if (reason == "kernel_panic_detected") has_kernel_panic = true;
        if (reason == "historical_crash_artifacts") has_historical = true;
    }
    CHECK_FALSE(has_kernel_panic);
    CHECK(has_historical);
    CHECK(state.summary.severity == Severity::Warn);
}

TEST_CASE("Aggregator: evaluate_crash severity truth table", "[aggregator]") {
    auto config = Config::defaults();
    SnapshotAggregator agg(config);

    // No panic artifacts (benign console log or empty pstore) → Ok.
    {
        CrashStatus c;
        c.present = true;  // an informational artifact may still be present
        c.panic_count = 0;
        CHECK(agg.evaluate_crash(c) == Severity::Ok);
    }
    // Current-boot unacked panic → Crit.
    {
        CrashStatus c;
        c.present = true;
        c.panic_count = 1;
        c.panic_current_boot = true;
        CHECK(agg.evaluate_crash(c) == Severity::Crit);
    }
    // Prior-boot unacked panic → Warn.
    {
        CrashStatus c;
        c.present = true;
        c.panic_count = 1;
        c.panic_current_boot = false;
        CHECK(agg.evaluate_crash(c) == Severity::Warn);
    }
    // Acknowledged panic (even current boot) → Ok (informational).
    {
        CrashStatus c;
        c.present = true;
        c.panic_count = 1;
        c.panic_current_boot = true;
        c.acknowledged = true;
        CHECK(agg.evaluate_crash(c) == Severity::Ok);
    }
}
