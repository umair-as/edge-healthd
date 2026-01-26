// SPDX-License-Identifier: MIT
// edge-healthd: Writer tests

#include <catch2/catch_test_macros.hpp>
#include "writer.hpp"

#include <filesystem>
#include <fstream>

using namespace edge;

TEST_CASE("SnapshotWriter creates output directory", "[writer]") {
    std::filesystem::path test_dir = "/tmp/edge-snapshot-test-writer";
    std::filesystem::remove_all(test_dir);

    SnapshotWriter writer(test_dir / "state.json");

    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.summary.severity = Severity::Ok;

    auto result = writer.write(state);
    CHECK(result.has_value());
    CHECK(std::filesystem::exists(test_dir / "state.json"));

    // Cleanup
    std::filesystem::remove_all(test_dir);
}

TEST_CASE("SnapshotWriter produces valid JSON", "[writer]") {
    std::filesystem::path test_file = "/tmp/edge-snapshot-test-output.json";
    std::filesystem::remove(test_file);

    SnapshotWriter writer(test_file);

    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.device.device_id = "test-device";
    state.device.hostname = "test-host";
    state.summary.severity = Severity::Ok;

    auto result = writer.write(state);
    REQUIRE(result.has_value());

    // Read back and verify it's valid JSON
    std::ifstream file(test_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    CHECK(content.find("edge.health.state") != std::string::npos);
    CHECK(content.find("test-device") != std::string::npos);
    CHECK(content.find("test-host") != std::string::npos);

    // Cleanup
    std::filesystem::remove(test_file);
}

TEST_CASE("SnapshotWriter to_json methods", "[writer]") {
    SnapshotWriter writer("/tmp/test.json");

    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.summary.severity = Severity::Warn;
    state.summary.reasons.push_back("boot_failures");

    auto json = writer.to_json(state);
    CHECK_FALSE(json.empty());
    CHECK(json.find("warn") != std::string::npos);
    CHECK(json.find("boot_failures") != std::string::npos);

    auto pretty = writer.to_json_pretty(state);
    CHECK_FALSE(pretty.empty());
    // Pretty should have newlines
    CHECK(pretty.find('\n') != std::string::npos);
}

TEST_CASE("WriterError formatting", "[writer]") {
    WriterError err{
        .message = "file not found",
        .code = 2
    };

    auto what = err.what();
    CHECK(what.find("file not found") != std::string::npos);
    CHECK(what.find("2") != std::string::npos);
}
