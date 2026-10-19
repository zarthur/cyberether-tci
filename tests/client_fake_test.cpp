#include "cyberether_tci/client.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace cyberether_tci;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto port = static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10));
    TciClient client;
    ClientConfig config;
    config.port = port;
    config.receiver = 0;
    config.requestedRateHz = 48000;
    config.queueCapacity = 16;
    if (!client.start(config)) return 3;

    StreamMetadata metadata{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        metadata = client.metadata();
        if (metadata.ready && metadata.iqStarted && metadata.sampleRateHz == 48000 &&
            metadata.panCenterHz == 14074000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!metadata.ready || !metadata.iqStarted || metadata.sampleRateHz != 48000 ||
        metadata.panCenterHz != 14074000) {
        client.stop();
        return 4;
    }

    std::array<std::complex<float>, 2> samples{};
    std::size_t copied = 0;
    while (std::chrono::steady_clock::now() < deadline && copied == 0) {
        copied = client.pop(samples);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (copied != 2 || samples[0] != std::complex<float>(0.25f, -0.5f) ||
        samples[1] != std::complex<float>(1.0f, 0.125f)) {
        client.stop();
        return 5;
    }

    metadata = {};
    while (std::chrono::steady_clock::now() < deadline) {
        metadata = client.metadata();
        if (metadata.ready && metadata.iqStarted && metadata.sampleRateHz == 48000 &&
            metadata.panCenterHz == 14075000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!metadata.ready || !metadata.iqStarted || metadata.panCenterHz != 14075000) {
        client.stop();
        return 6;
    }
    samples = {};
    copied = 0;
    while (std::chrono::steady_clock::now() < deadline && copied == 0) {
        copied = client.pop(samples);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    client.stop();
    if (copied != 2 || samples[0] != std::complex<float>(-0.25f, 0.5f) ||
        samples[1] != std::complex<float>(0.75f, -0.125f)) return 7;
    std::cout << "fake TCI WebSocket reconnect lifecycle passed\n";
    return 0;
}
