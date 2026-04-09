// SPDX-License-Identifier: MIT
// edge-healthd: JSON serialization tests

#include <catch2/catch_test_macros.hpp>
#include "json.hpp"

#include <nlohmann/json.hpp>

using namespace edge;

TEST_CASE("JSON serialize DeviceInfo", "[json]") {
    DeviceInfo device;
    device.device_id = "dev-001";
    device.hostname = "test-host";
    device.platform = "rpi5";
    device.arch = "aarch64";
    device.os.distro = "poky";
    device.os.version = "4.0";
    device.os.kernel = "6.1.0";

    auto j = nlohmann::json(device);

    CHECK(j["device_id"] == "dev-001");
    CHECK(j["hostname"] == "test-host");
    CHECK(j["platform"] == "rpi5");
    CHECK(j["arch"] == "aarch64");
    CHECK(j["os"]["distro"] == "poky");
}

TEST_CASE("JSON serialize BootStatus", "[json]") {
    BootStatus boot;
    boot.boot_id = "abc123";
    boot.last_boot_at = std::chrono::system_clock::now();
    boot.uptime = std::chrono::seconds(3600);
    boot.boot_ok = true;
    boot.boot_fail_count = 0;

    auto j = nlohmann::json(boot);

    CHECK(j["boot_id"] == "abc123");
    CHECK(j["uptime"] == 3600);
    CHECK(j["boot_ok"] == true);
    CHECK(j["boot_fail_count"] == 0);
}

TEST_CASE("JSON serialize ServicesStatus", "[json]") {
    ServicesStatus services;
    services.overall = Severity::Ok;

    ServiceUnit unit;
    unit.name = "sshd.service";
    unit.state = ServiceState::Active;
    unit.severity = Severity::Ok;
    unit.restart_count = 0;
    services.units.push_back(unit);

    auto j = nlohmann::json(services);

    CHECK(j["overall"] == "ok");
    CHECK(j["units"].size() == 1);
    CHECK(j["units"][0]["name"] == "sshd.service");
    CHECK(j["units"][0]["state"] == "active");
    CHECK(j["units"][0]["severity"] == "ok");
}

TEST_CASE("JSON serialize ResourcesStatus", "[json]") {
    ResourcesStatus resources;
    resources.sample_window_sec = 60;
    resources.cpu.load1 = 0.5;
    resources.cpu.load5 = 0.4;
    resources.cpu.load15 = 0.3;
    resources.memory.mem_total_mb = 4096;
    resources.memory.mem_used_mb = 2048;

    StorageMount mount;
    mount.mount = "/";
    mount.fs = "ext4";
    mount.used_pct = 50;
    mount.avail_mb = 10000;
    resources.storage.push_back(mount);

    NetworkInterface iface;
    iface.ifname = "eth0";
    iface.link = LinkState::Up;
    iface.rx_bytes = 1200;
    iface.tx_bytes = 900;
    iface.rx_packets = 12;
    iface.tx_packets = 9;
    iface.rx_dropped = 1;
    iface.tx_dropped = 2;
    iface.rx_err = 3;
    iface.tx_err = 4;
    resources.network.push_back(iface);

    auto j = nlohmann::json(resources);

    CHECK(j["cpu"]["load1"] == 0.5);
    CHECK(j["memory"]["mem_total_mb"] == 4096);
    CHECK(j["storage"][0]["mount"] == "/");
    CHECK(j["network"][0]["ifname"] == "eth0");
    CHECK(j["network"][0]["rx_bytes"] == 1200);
    CHECK(j["network"][0]["tx_bytes"] == 900);
    CHECK(j["network"][0]["rx_packets"] == 12);
    CHECK(j["network"][0]["tx_packets"] == 9);
    CHECK(j["network"][0]["rx_dropped"] == 1);
    CHECK(j["network"][0]["tx_dropped"] == 2);
    CHECK(j["network"][0]["rx_err"] == 3);
    CHECK(j["network"][0]["tx_err"] == 4);
}

TEST_CASE("JSON serialize full SnapshotState", "[json]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.device.device_id = "test";
    state.summary.severity = Severity::Ok;
    state.summary.reasons = {};

    auto j = nlohmann::json(state);

    CHECK(j["schema"] == "edge.health.state");
    CHECK(j["schema_version"] == "1.0");
    CHECK(j.contains("generated_at"));
    CHECK(j.contains("device"));
    CHECK(j.contains("boot"));
    CHECK(j.contains("services"));
    CHECK(j.contains("resources"));
    CHECK(j.contains("time_sync"));
    CHECK(j.contains("update"));
    CHECK(j.contains("summary"));
}

TEST_CASE("JSON ptp.enabled reflects config in snapshot", "[json][ptp]") {
    TimeSyncStatus ts;
    ts.ptp.enabled = true;
    ts.overall = Severity::Ok;

    auto j = nlohmann::json(ts);

    CHECK(j["ptp"]["enabled"] == true);
}

TEST_CASE("JSON ptp.enabled false by default", "[json][ptp]") {
    TimeSyncStatus ts;

    auto j = nlohmann::json(ts);

    CHECK(j["ptp"]["enabled"] == false);
}

TEST_CASE("JSON cycle field is serialized and increments", "[json]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.cycle = 0;

    auto j0 = nlohmann::json(state);
    CHECK(j0["cycle"] == 0);

    state.cycle = 42;
    auto j42 = nlohmann::json(state);
    CHECK(j42["cycle"] == 42);
}

TEST_CASE("JSON serialize_pretty produces indented output", "[json]") {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();

    auto compact = json::serialize(state);
    auto pretty = json::serialize_pretty(state, 2);

    // Pretty should be longer due to whitespace
    CHECK(pretty.size() > compact.size());
    // Pretty should have newlines
    CHECK(pretty.find('\n') != std::string::npos);
}
