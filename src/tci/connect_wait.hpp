#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>

namespace cyberether_tci::detail {

enum class ConnectWaitResult { Writable, Cancelled, TimedOut, Error };

template <typename Clock, typename Poll>
ConnectWaitResult waitForWritableUntil(const std::atomic<bool>& stopping,
                                       typename Clock::time_point deadline,
                                       Poll&& pollSocket) {
    constexpr auto pollInterval = std::chrono::milliseconds(50);
    while (!stopping.load()) {
        // Take one snapshot per iteration and check expiry before subtracting.
        // The poll timeout can therefore never become negative across a
        // preemption at the deadline (where poll would otherwise wait forever).
        const auto now = Clock::now();
        if (now >= deadline) return ConnectWaitResult::TimedOut;

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto timeout = std::min(pollInterval, std::max(std::chrono::milliseconds::zero(), remaining));
        const int result = pollSocket(timeout);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return ConnectWaitResult::Error;
        if (result > 0) return ConnectWaitResult::Writable;
    }
    return ConnectWaitResult::Cancelled;
}

}  // namespace cyberether_tci::detail
