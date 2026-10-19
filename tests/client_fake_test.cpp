#include "cyberether_tci/client.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace cyberether_tci;

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const auto port = static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10));
    const auto requestedRate = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const auto changedRate = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
    if (!isFlex6400IqRate(requestedRate) || !isFlex6400IqRate(changedRate)) return 3;

    ClientConfig hostnameConfig;
    hostnameConfig.host = "localhost";
    TciClient hostnameClient;
    if (hostnameClient.start(hostnameConfig)) {
        hostnameClient.stop();
        return 4;
    }

    TciClient client;
    ClientConfig config;
    config.port = port;
    config.receiver = 0;
    config.requestedRateHz = requestedRate;
    config.queueCapacity = 16;
    if (!client.start(config)) return 5;

    const auto waitForMetadata = [&](std::uint32_t rate, std::uint32_t center,
                                     std::chrono::milliseconds timeout,
                                     StreamMetadata& metadata) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            metadata = client.metadata();
            if (metadata.connected && metadata.ready && metadata.iqStarted &&
                metadata.sampleRateHz == rate && metadata.panCenterHz == center) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        metadata = client.metadata();
        return metadata.connected && metadata.ready && metadata.iqStarted &&
               metadata.sampleRateHz == rate && metadata.panCenterHz == center;
    };

    StreamMetadata initial{};
    if (!waitForMetadata(requestedRate, 14074000, std::chrono::seconds(2), initial)) {
        client.stop();
        return 6;
    }

    StreamMetadata changed{};
    if (!waitForMetadata(changedRate, 14075000, std::chrono::seconds(2), changed) ||
        changed.epoch <= initial.epoch) {
        client.stop();
        return 7;
    }

    std::array<std::complex<float>, 3> samples;
    samples.fill({-9.0f, -9.0f});
    if (client.waitForSamples(samples.size(), std::chrono::milliseconds(100))) {
        client.stop();
        return 8; // fake server has sent only two samples in the new epoch
    }
    auto partial = client.popWithMetadata(samples);
    if (partial.samplesCopied != 0 || partial.metadata.epoch != changed.epoch ||
        samples[0] != std::complex<float>(-9.0f, -9.0f) ||
        samples[2] != std::complex<float>(-9.0f, -9.0f)) {
        client.stop();
        return 9;
    }

    if (!client.waitForSamples(samples.size(), std::chrono::seconds(2))) {
        client.stop();
        return 10;
    }
    const auto changedRead = client.popWithMetadata(samples);
    if (changedRead.samplesCopied != samples.size() ||
        changedRead.metadata.sampleRateHz != changedRate ||
        changedRead.metadata.panCenterHz != 14075000 ||
        changedRead.metadata.epoch != changed.epoch ||
        samples[0] != std::complex<float>(0.75f, -0.25f) ||
        samples[1] != std::complex<float>(0.5f, 0.125f) ||
        samples[2] != std::complex<float>(-0.25f, 0.75f)) {
        client.stop();
        return 11;
    }

    StreamMetadata reconnected{};
    if (!waitForMetadata(requestedRate, 14076000, std::chrono::seconds(3), reconnected) ||
        reconnected.epoch <= changedRead.metadata.epoch ||
        !client.waitForSamples(samples.size(), std::chrono::seconds(2))) {
        client.stop();
        return 12;
    }
    const auto reconnectRead = client.popWithMetadata(samples);
    client.stop();
    if (reconnectRead.samplesCopied != samples.size() ||
        reconnectRead.metadata.sampleRateHz != requestedRate ||
        reconnectRead.metadata.panCenterHz != 14076000 ||
        reconnectRead.metadata.epoch != reconnected.epoch ||
        samples[0] != std::complex<float>(-0.5f, 0.5f) ||
        samples[1] != std::complex<float>(0.75f, -0.75f) ||
        samples[2] != std::complex<float>(0.125f, -0.125f)) return 13;

    std::cout << "fake TCI lifecycle and metadata-epoch checks passed at "
              << requestedRate << " -> " << changedRate << " samples/s\n";
    return 0;
}
