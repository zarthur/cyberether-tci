#pragma once

#include "cyberether_tci/protocol.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace cyberether_tci {

struct ClientConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 50001;
    std::uint32_t receiver = 0;
    std::uint32_t requestedRateHz = 48000;
    std::size_t queueCapacity = 262144;
};

struct StreamMetadata {
    std::uint32_t sampleRateHz = 0;
    std::uint32_t panCenterHz = 0;
    bool ready = false;
    bool iqStarted = false;
    bool connected = false;
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
    std::size_t pop(std::span<std::complex<float>> output) noexcept;
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
    mutable std::mutex queueMutex_;
    BoundedIqQueue queue_{1};
    mutable std::mutex waitMutex_;
    std::condition_variable wake_;
    std::atomic<std::uint32_t> sampleRateHz_{0};
    std::atomic<std::uint32_t> panCenterHz_{0};
    std::atomic<bool> ready_{false};
    std::atomic<bool> iqStarted_{false};
    std::atomic<bool> connected_{false};
};

}  // namespace cyberether_tci
