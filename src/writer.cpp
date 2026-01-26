// SPDX-License-Identifier: MIT
// edge-healthd: Writer implementation

#include "json.hpp"
#include "writer.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <unistd.h>

namespace edge {

SnapshotWriter::SnapshotWriter(std::filesystem::path output_path)
    : output_path_(std::move(output_path)) {}

WriterResult<> SnapshotWriter::write(const SnapshotState& state) const {
    // Ensure directory exists
    if (auto result = ensure_directory(); !result) {
        return result;
    }

    // Serialize
    std::string content = pretty_ ? to_json_pretty(state) : to_json(state);

    // Write atomically
    return write_atomic(content);
}

std::string SnapshotWriter::to_json(const SnapshotState& state) const {
    return json::serialize(state);
}

std::string SnapshotWriter::to_json_pretty(const SnapshotState& state, int indent) const {
    return json::serialize_pretty(state, indent);
}

WriterResult<> SnapshotWriter::ensure_directory() const {
    auto dir = output_path_.parent_path();

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    if (ec) {
        return std::unexpected(WriterError{
            .message = "Failed to create directory: " + dir.string(),
            .code = ec.value()
        });
    }

    return {};
}

WriterResult<> SnapshotWriter::write_atomic(const std::string& content) const {
    auto tmp = temp_path();

    // Write to temp file
    {
        std::ofstream file(tmp, std::ios::binary);
        if (!file) {
            return std::unexpected(WriterError{
                .message = "Failed to open temp file: " + tmp.string(),
                .code = errno
            });
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!file) {
            return std::unexpected(WriterError{
                .message = "Failed to write temp file",
                .code = errno
            });
        }

        file.flush();

        // fsync the file
        // Note: This is a simplified version; production code would use
        // file descriptor and fsync() directly
    }

    // Rename to final path (atomic on POSIX)
    std::error_code ec;
    std::filesystem::rename(tmp, output_path_, ec);

    if (ec) {
        // Clean up temp file
        std::filesystem::remove(tmp);

        return std::unexpected(WriterError{
            .message = "Failed to rename temp file to " + output_path_.string(),
            .code = ec.value()
        });
    }

    return {};
}

std::filesystem::path SnapshotWriter::temp_path() const {
    return output_path_.string() + ".tmp." + std::to_string(getpid());
}

} // namespace edge
