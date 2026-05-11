// SPDX-License-Identifier: MIT
// edge-healthd: Atomic file writer
//
// Single primitive that both SnapshotWriter and BootProbe::save_boot_state
// share. Without this, each call site grew an ofstream + rename pair that
// missed fsync — a power cut between rename returning and writeback could
// leave a zero-length file on disk (issues #37, #40).
//
// Ordering:
//   open(temp, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC)
//   write loop (EINTR-safe)
//   fsync(fd)
//   close(fd)
//   rename(temp, target)      <-- atomic on POSIX
//   fsync(parent_dir_fd)      <-- so the rename survives power loss
//
// On any failure the temp file is cleaned up before returning the error.

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include <sys/types.h>

namespace edge {

struct AtomicFileError {
    std::string message;
    int code = 0;  // errno

    [[nodiscard]] std::string what() const {
        if (code != 0) {
            return message + " (errno " + std::to_string(code) + ")";
        }
        return message;
    }
};

template <typename T = void>
using AtomicFileResult = std::expected<T, AtomicFileError>;

/// Atomically replace `target` with `content`. The temp file's name is
/// derived from `target` + ".tmp." + getpid() so two daemons writing the
/// same target collide on rename rather than on the temp file.
///
/// `mode` is the open(2) mode applied to the temp file before rename;
/// the umask still applies.
[[nodiscard]] AtomicFileResult<>
atomic_write_file(const std::filesystem::path& target,
                  std::string_view content,
                  ::mode_t mode = 0640);

}  // namespace edge
