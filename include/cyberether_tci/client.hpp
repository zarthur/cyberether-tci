#pragma once

#include "cyberether_tci/protocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace cyberether_tci {

struct ClientConfig {
    // The initial implementation only accepts this literal IPv4 loopback host;
    // hostnames and remote endpoints are intentionally rejected.
    std::string host = "127.0.0.1";
    std::uint16_t port = 50001;
    std::uint32_t receiver = 0;
    std::uint32_t requestedRateHz = 48000;
    std::size_t queueCapacity = 262144;
};

struct StreamMetadata {
    std::uint32_t sampleRateHz = 0;
    std::uint32_t panCenterHz = 0;
    std::uint64_t epoch = 0;
    bool ready = false;
    bool iqStarted = false;
    bool connected = false;
};

struct StreamReadResult {
    StreamMetadata metadata;
    std::size_t samplesCopied = 0;
};

// Minimal ws:// TCI IQ client. Its only outbound protocol records are
// iq_samplerate, iq_start, and iq_stop; it has no generic command API.
class TciClient {
  public:
    TciClient() = default;
    ~TciClient();
    TciClient(const TciClient&) = delete;
    TciClient& operator=(const TciClient&) = delete;

    bool start(ClientConfig config);
    void stop() noexcept;
    bool waitForSamples(std::size_t required, std::chrono::milliseconds timeout) const noexcept;
    StreamReadResult popWithMetadata(std::span<std::complex<float>> output) noexcept;
    StreamMetadata metadata() const noexcept;

  private:
    void run();
    bool runConnection();
    void clearSession() noexcept;

    std::atomic<bool> stopping_{false};
    std::atomic<int> socketFd_{-1};
    std::thread worker_;
    ClientConfig config_;
    mutable std::mutex sendMutex_;
    mutable std::mutex stateMutex_;
    BoundedIqQueue queue_{1};
    mutable std::mutex waitMutex_;
    std::condition_variable wake_;
    mutable std::condition_variable stateChanged_;
    StreamMetadata metadata_;
};

}  // namespace cyberether_tci
