// SPDX-License-Identifier: MIT
// edge-healthd: State writer
// Serializes SnapshotState to JSON and writes atomically

#pragma once

#include "types.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace edge {

// -----------------------------------------------------------------------------
// Writer error type
// -----------------------------------------------------------------------------

struct WriterError {
    std::string message;
    int code = 0;  // errno if applicable

    [[nodiscard]] std::string what() const {
        if (code != 0) {
            return message + " (errno " + std::to_string(code) + ")";
        }
        return message;
    }
};

template <typename T = void>
using WriterResult = std::expected<T, WriterError>;

// -----------------------------------------------------------------------------
// SnapshotWriter
//
// Writes SnapshotState to a JSON file using atomic rename pattern:
// 1. Write to temporary file
// 2. fsync() the file
// 3. rename() to final path (atomic on POSIX)
// 4. fsync() the directory
// -----------------------------------------------------------------------------

class SnapshotWriter {
public:
    /// Construct writer for given output path
    explicit SnapshotWriter(std::filesystem::path output_path);

    // -------------------------------------------------------------------------
    // Main interface
    // -------------------------------------------------------------------------

    /// Write state to file atomically
    [[nodiscard]] WriterResult<> write(const SnapshotState& state) const;

    /// Write state to string (for testing or alternate output)
    [[nodiscard]] std::string to_json(const SnapshotState& state) const;

    /// Write state to string with pretty formatting
    [[nodiscard]] std::string to_json_pretty(const SnapshotState& state, int indent = 2) const;

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// Get current output path
    [[nodiscard]] const std::filesystem::path& output_path() const noexcept {
        return output_path_;
    }

    /// Set output path
    void set_output_path(std::filesystem::path path) {
        output_path_ = std::move(path);
    }

    /// Enable/disable pretty printing (default: false for minimal size)
    void set_pretty(bool enable) noexcept { pretty_ = enable; }
    [[nodiscard]] bool pretty() const noexcept { return pretty_; }

private:
    std::filesystem::path output_path_;
    bool pretty_ = false;

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Ensure parent directory exists
    [[nodiscard]] WriterResult<> ensure_directory() const;

    /// Write content to file atomically
    [[nodiscard]] WriterResult<> write_atomic(const std::string& content) const;
};

} // namespace edge
