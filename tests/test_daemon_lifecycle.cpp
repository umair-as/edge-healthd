// SPDX-License-Identifier: MIT
// edge-healthd: Shutdown & watchdog lifecycle tests
//
// Exercises the two low-level primitives behind the SIGTERM/SIGINT shutdown
// path and the watchdog thread, verifying both wake promptly instead of waiting
// out their full timeouts (which is what risked systemd's SIGKILL).

#include <catch2/catch_test_macros.hpp>
#include "daemon.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <stop_token>
#include <thread>

using namespace edge;
using namespace std::chrono_literals;

namespace {
void signal_eventfd(int fd) {
    const uint64_t one = 1;
    const ssize_t r = write(fd, &one, sizeof(one));
    REQUIRE(r == static_cast<ssize_t>(sizeof(one)));
}
} // namespace

TEST_CASE("wait_wakeup_fd returns immediately when fd is already signaled",
          "[daemon][shutdown]") {
    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    REQUIRE(fd >= 0);

    signal_eventfd(fd);  // as the signal handler / D-Bus trigger would

    const auto start = std::chrono::steady_clock::now();
    const bool woke = detail::wait_wakeup_fd(fd, 60s);  // huge timeout; must not wait it out
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(woke);
    CHECK(elapsed < 1s);
    ::close(fd);
}

TEST_CASE("wait_wakeup_fd wakes when the fd is signaled from another thread",
          "[daemon][shutdown]") {
    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    REQUIRE(fd >= 0);

    std::thread writer([fd] {
        std::this_thread::sleep_for(50ms);
        signal_eventfd(fd);
    });

    const auto start = std::chrono::steady_clock::now();
    const bool woke = detail::wait_wakeup_fd(fd, 60s);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(woke);
    CHECK(elapsed < 5s);  // woke on the write, not the 60s timeout

    writer.join();
    ::close(fd);
}

TEST_CASE("wait_wakeup_fd times out and drains when the fd stays idle",
          "[daemon][shutdown]") {
    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    REQUIRE(fd >= 0);

    const auto start = std::chrono::steady_clock::now();
    const bool woke = detail::wait_wakeup_fd(fd, 50ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(woke);
    CHECK(elapsed >= 40ms);
    ::close(fd);
}

TEST_CASE("watchdog_sleep is interrupted promptly by a stop request",
          "[daemon][watchdog]") {
    std::condition_variable_any cv;
    std::stop_source src;

    std::atomic<bool> returned{false};
    std::atomic<bool> stop_seen{false};

    const auto start = std::chrono::steady_clock::now();
    std::thread waiter([&] {
        // Would sleep 60s if the stop request did not wake it.
        const bool stopped = detail::watchdog_sleep(cv, src.get_token(), 60s);
        stop_seen.store(stopped);
        returned.store(true);
    });

    std::this_thread::sleep_for(50ms);
    src.request_stop();
    waiter.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(returned.load());
    CHECK(stop_seen.load());
    CHECK(elapsed < 5s);  // returned on the stop request, not the 60s timeout
}

TEST_CASE("watchdog_sleep times out and returns false when not stopped",
          "[daemon][watchdog]") {
    std::condition_variable_any cv;
    std::stop_source src;

    const auto start = std::chrono::steady_clock::now();
    const bool stopped = detail::watchdog_sleep(cv, src.get_token(), 50ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(stopped);
    CHECK(elapsed >= 40ms);
}

TEST_CASE("watchdog_sleep returns immediately if stop already requested",
          "[daemon][watchdog]") {
    std::condition_variable_any cv;
    std::stop_source src;
    src.request_stop();

    const auto start = std::chrono::steady_clock::now();
    const bool stopped = detail::watchdog_sleep(cv, src.get_token(), 60s);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(stopped);
    CHECK(elapsed < 1s);
}
