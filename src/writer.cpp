// SPDX-License-Identifier: MIT
// edge-healthd: Writer implementation

#include "atomic_file.hpp"
#include "json.hpp"
#include "writer.hpp"

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
    auto result = atomic_write_file(output_path_, content);
    if (!result) {
        return std::unexpected(WriterError{
            .message = result.error().message,
            .code = result.error().code,
        });
    }
    return {};
}

} // namespace edge
