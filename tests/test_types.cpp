// SPDX-License-Identifier: MIT
// edge-healthd: Type tests

#include <catch2/catch_test_macros.hpp>
#include "types.hpp"

using namespace edge;

TEST_CASE("Severity to_string", "[types]") {
    CHECK(to_string(Severity::Ok) == "ok");
    CHECK(to_string(Severity::Warn) == "warn");
    CHECK(to_string(Severity::Crit) == "crit");
    CHECK(to_string(Severity::Unknown) == "unknown");
}

TEST_CASE("ServiceState to_string", "[types]") {
    CHECK(to_string(ServiceState::Active) == "active");
    CHECK(to_string(ServiceState::Inactive) == "inactive");
    CHECK(to_string(ServiceState::Failed) == "failed");
    CHECK(to_string(ServiceState::Activating) == "activating");
    CHECK(to_string(ServiceState::Deactivating) == "deactivating");
    CHECK(to_string(ServiceState::Unknown) == "unknown");
}

TEST_CASE("LinkState to_string", "[types]") {
    CHECK(to_string(LinkState::Up) == "up");
    CHECK(to_string(LinkState::Down) == "down");
    CHECK(to_string(LinkState::Unknown) == "unknown");
}

TEST_CASE("TimeSyncSource to_string", "[types]") {
    CHECK(to_string(TimeSyncSource::None) == "none");
    CHECK(to_string(TimeSyncSource::Ntp) == "ntp");
    CHECK(to_string(TimeSyncSource::Ptp) == "ptp");
}

TEST_CASE("SnapshotState has correct schema", "[types]") {
    CHECK(SnapshotState::schema == "edge.health.state");
    CHECK(SnapshotState::schema_version == "1.0");
}

TEST_CASE("Default values are sensible", "[types]") {
    BootStatus boot;
    CHECK(boot.boot_ok == true);
    CHECK(boot.boot_fail_count == 0);

    ServiceUnit unit;
    CHECK(unit.state == ServiceState::Unknown);
    CHECK(unit.severity == Severity::Unknown);
    CHECK(unit.restart_count == 0);

    CpuLoad cpu;
    CHECK(cpu.load1 == 0.0);
    CHECK(cpu.load5 == 0.0);
    CHECK(cpu.load15 == 0.0);
}
