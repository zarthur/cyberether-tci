#include "tci/connect_wait.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition << '\n'; \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

struct ManualClock {
    using rep = std::chrono::milliseconds::rep;
    using period = std::chrono::milliseconds::period;
    using duration = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<ManualClock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept { return current; }
    static inline time_point current{};
};

int main() {
    using namespace std::chrono_literals;
    using cyberether_tci::detail::ConnectWaitResult;
    using cyberether_tci::detail::waitForWritableUntil;

    const auto start = ManualClock::time_point{};
    std::atomic<bool> stopping{false};
    std::vector<std::chrono::milliseconds> timeouts;
    ManualClock::current = start;
    const auto deadline = start + 10ms;
    const auto expired = waitForWritableUntil<ManualClock>(stopping, deadline, [&](auto timeout) {
        timeouts.push_back(timeout);
        // Simulate preemption while connect remains pending, crossing deadline
        // before the poll callback returns.
        ManualClock::current = deadline + 1ms;
        return 0;
    });
    CHECK(expired == ConnectWaitResult::TimedOut);
    CHECK(timeouts.size() == 1);
    CHECK(timeouts.front() == 10ms);
    CHECK(timeouts.front() >= 0ms);

    stopping.store(false);
    ManualClock::current = start;
    std::size_t pollCalls = 0;
    std::chrono::milliseconds cancellationTimeout{-1};
    const auto cancelled = waitForWritableUntil<ManualClock>(stopping, start + 1s, [&](auto timeout) {
        ++pollCalls;
        cancellationTimeout = timeout;
        // Simulate stop arriving while a nonblocking connect is still pending.
        stopping.store(true);
        return 0;
    });
    CHECK(cancelled == ConnectWaitResult::Cancelled);
    CHECK(pollCalls == 1);
    CHECK(cancellationTimeout >= 0ms);
    std::cout << "pending connect deadline expiry and cancellation passed\n";
    return 0;
}
