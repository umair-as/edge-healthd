// SPDX-License-Identifier: MIT
// edge-healthd: Atomic file writer implementation

#include "atomic_file.hpp"

#include <cerrno>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace edge {

namespace {

AtomicFileError make_error(std::string message, int code) {
    return AtomicFileError{.message = std::move(message), .code = code};
}

std::filesystem::path temp_path_for(const std::filesystem::path& target) {
    return target.string() + ".tmp." + std::to_string(::getpid());
}

}  // namespace

AtomicFileResult<>
atomic_write_file(const std::filesystem::path& target,
                  std::string_view content,
                  ::mode_t mode) {
    namespace fs = std::filesystem;

    fs::path parent = target.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    const fs::path tmp = temp_path_for(target);

    const int fd = ::open(tmp.c_str(),
                          O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
                          mode);
    if (fd < 0) {
        return std::unexpected(make_error(
            "open(" + tmp.string() + ") failed", errno));
    }

    // Any failure after this point must clean up the temp file.
    auto fail = [&](std::string msg, int code) -> AtomicFileResult<> {
        std::error_code ec;
        fs::remove(tmp, ec);
        return std::unexpected(make_error(std::move(msg), code));
    };

    const char* p = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved = errno;
            ::close(fd);
            return fail("write(" + tmp.string() + ") failed", saved);
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }

    if (::fsync(fd) != 0) {
        const int saved = errno;
        ::close(fd);
        return fail("fsync(" + tmp.string() + ") failed", saved);
    }

    if (::close(fd) != 0) {
        return fail("close(" + tmp.string() + ") failed", errno);
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        return fail("rename(" + tmp.string() + " -> " + target.string() +
                        ") failed",
                    ec.value());
    }

    // Rename is now durable in the inode table but may not be in the
    // directory entry until the parent dir's metadata is flushed.
    const int dfd = ::open(parent.c_str(),
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) {
        return std::unexpected(make_error(
            "open(" + parent.string() + ") for dir fsync failed", errno));
    }
    if (::fsync(dfd) != 0) {
        const int saved = errno;
        ::close(dfd);
        return std::unexpected(make_error(
            "fsync(" + parent.string() + ") failed", saved));
    }
    ::close(dfd);

    return {};
}

}  // namespace edge
