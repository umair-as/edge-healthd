// SPDX-License-Identifier: MIT
// edge-healthd: atomic_write_file tests

#include <catch2/catch_test_macros.hpp>
#include "atomic_file.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace edge;

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("atomic_write_file writes content to target", "[atomic_file]") {
    std::filesystem::path target = "/tmp/edge-atomic-write-1.txt";
    std::filesystem::remove(target);

    auto result = atomic_write_file(target, "hello world");
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(target));
    CHECK(read_file(target) == "hello world");

    std::filesystem::remove(target);
}

TEST_CASE("atomic_write_file replaces existing content", "[atomic_file]") {
    std::filesystem::path target = "/tmp/edge-atomic-write-2.txt";
    {
        std::ofstream f(target);
        f << "old contents that are longer than the replacement";
    }

    auto result = atomic_write_file(target, "new");
    REQUIRE(result.has_value());
    CHECK(read_file(target) == "new");

    std::filesystem::remove(target);
}

TEST_CASE("atomic_write_file fails when parent directory is missing",
          "[atomic_file]") {
    std::filesystem::path target =
        "/tmp/edge-atomic-write-no-such-dir/file.txt";
    std::filesystem::remove_all(target.parent_path());

    auto result = atomic_write_file(target, "x");
    REQUIRE_FALSE(result.has_value());
    // ENOENT-class error — exact code is platform-dependent for std::fs.
    CHECK(result.error().code != 0);
    CHECK_FALSE(std::filesystem::exists(target));
}

TEST_CASE("atomic_write_file leaves no temp file behind on success",
          "[atomic_file]") {
    std::filesystem::path target = "/tmp/edge-atomic-write-3.txt";
    std::filesystem::remove(target);

    auto result = atomic_write_file(target, "done");
    REQUIRE(result.has_value());

    // Walk parent and confirm no stale temp file matching the pattern.
    int stale = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(target.parent_path())) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(target.filename().string() + ".tmp.")) {
            ++stale;
        }
    }
    CHECK(stale == 0);

    std::filesystem::remove(target);
}

TEST_CASE("atomic_write_file handles a larger payload across writes",
          "[atomic_file]") {
    std::filesystem::path target = "/tmp/edge-atomic-write-4.bin";
    std::filesystem::remove(target);

    // 256 KiB — exercises the write-loop path even if the kernel writes
    // less than requested.
    std::string payload(256 * 1024, '\xA5');

    auto result = atomic_write_file(target, payload);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::file_size(target) == payload.size());
    CHECK(read_file(target) == payload);

    std::filesystem::remove(target);
}

TEST_CASE("atomic_write_file produces empty file for empty content",
          "[atomic_file]") {
    std::filesystem::path target = "/tmp/edge-atomic-write-5.txt";
    {
        std::ofstream f(target);
        f << "stale";
    }

    auto result = atomic_write_file(target, "");
    REQUIRE(result.has_value());
    CHECK(std::filesystem::file_size(target) == 0);

    std::filesystem::remove(target);
}
