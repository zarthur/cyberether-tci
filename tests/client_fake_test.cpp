#include "cyberether_tci/client.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace cyberether_tci;

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const auto port = static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10));
    const auto requestedRate = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const auto changedRate = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
    if (!isFlex6400IqRate(requestedRate) || !isFlex6400IqRate(changedRate)) return 3;
    const std::filesystem::path markerDir(argv[4]);
    const auto releaseFixture = [&markerDir](const char* stage) {
        std::ofstream(markerDir / stage) << "ready\n";
    };

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
    config.queueCapacity = 4;
    if (!client.start(config)) return 5;

    const auto waitForMetadata = [&](std::uint32_t rate, std::uint32_t center,
                                     std::chrono::milliseconds timeout,
                                     StreamMetadata& metadata, bool requireIqStarted) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            metadata = client.metadata();
            if (metadata.connected && metadata.ready &&
                (!requireIqStarted || metadata.iqStarted) &&
                metadata.sampleRateHz == rate && metadata.panCenterHz == center) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        metadata = client.metadata();
        return metadata.connected && metadata.ready &&
               (!requireIqStarted || metadata.iqStarted) &&
               metadata.sampleRateHz == rate && metadata.panCenterHz == center;
    };

    StreamMetadata initial{};
    // The first connection intentionally has valid readiness/rate/center
    // metadata but no iq_start echo. Binary IQ alone may confirm streaming,
    // but only after our explicit start request and a valid matching frame.
    if (!waitForMetadata(requestedRate, 14074000, std::chrono::seconds(2), initial, false) ||
        initial.iqStarted) {
        client.stop();
        return 6;
    }
    std::array<std::complex<float>, 1> rejectedSample;
    rejectedSample.fill({-9.0f, -9.0f});
    if (client.waitForSamples(1, std::chrono::milliseconds(100))) {
        client.stop();
        return 7; // pre-ready/wrong-receiver/wrong-rate/malformed frames are not IQ
    }
    const auto rejectedRead = client.popWithMetadata(rejectedSample);
    if (rejectedRead.samplesCopied != 0 ||
        rejectedSample[0] != std::complex<float>(-9.0f, -9.0f)) {
        client.stop();
        return 8;
    }
    releaseFixture("activate");
    if (!waitForMetadata(requestedRate, 14074000, std::chrono::seconds(2), initial, true) ||
        !client.waitForSamples(4, std::chrono::seconds(2))) {
        client.stop();
        return 9;
    }
    auto diagnostics = client.diagnostics();
    if (diagnostics.overflowDroppedSamplePairs != 2 ||
        diagnostics.metadataEpochFlushedSamplePairs != 0) {
        client.stop();
        return 10; // overflow counts CF32 pairs without counting an epoch flush
    }
    // The six-sample frame overflowed the four-sample queue by two. Read only
    // two, leaving two queued pairs for the confirmed metadata epoch flush.
    std::array<std::complex<float>, 2> initialSamples{};
    const auto initialRead = client.popWithMetadata(initialSamples);
    if (initialRead.samplesCopied != initialSamples.size() ||
        initialRead.metadata.epoch != initial.epoch ||
        initialSamples[0] != std::complex<float>(-0.625f, -0.125f) ||
        initialSamples[1] != std::complex<float>(0.125f, 0.25f)) {
        client.stop();
        return 11;
    }
    releaseFixture("initial-read");

    StreamMetadata changed{};
    if (!waitForMetadata(changedRate, 14075000, std::chrono::seconds(2), changed, true) ||
        changed.epoch <= initial.epoch) {
        client.stop();
        return 12;
    }
    diagnostics = client.diagnostics();
    if (diagnostics.overflowDroppedSamplePairs != 2 ||
        diagnostics.metadataEpochFlushedSamplePairs != 2) {
        client.stop();
        return 13; // a metadata update reports queued-pair flushes separately
    }

    std::array<std::complex<float>, 3> samples;
    samples.fill({-9.0f, -9.0f});
    if (client.waitForSamples(samples.size(), std::chrono::milliseconds(100))) {
        client.stop();
        return 14; // stale old-epoch sample must be cleared; only two new samples arrived
    }
    auto partial = client.popWithMetadata(samples);
    if (partial.samplesCopied != 0 || partial.metadata.epoch != changed.epoch ||
        samples[0] != std::complex<float>(-9.0f, -9.0f) ||
        samples[2] != std::complex<float>(-9.0f, -9.0f)) {
        client.stop();
        return 15;
    }
    releaseFixture("partial-read");

    if (!client.waitForSamples(samples.size(), std::chrono::seconds(2))) {
        client.stop();
        return 16;
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
        return 17;
    }
    releaseFixture("check-stop");

    const auto stoppedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    StreamMetadata stopped{};
    while (std::chrono::steady_clock::now() < stoppedDeadline) {
        stopped = client.metadata();
        if (stopped.connected && stopped.ready && !stopped.iqStarted &&
            stopped.sampleRateHz == changedRate && stopped.panCenterHz == 14075000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    rejectedSample.fill({-9.0f, -9.0f});
    const auto staleRead = client.popWithMetadata(rejectedSample);
    if (!stopped.connected || stopped.iqStarted || staleRead.samplesCopied != 0 ||
        client.waitForSamples(1, std::chrono::milliseconds(100)) ||
        rejectedSample[0] != std::complex<float>(-9.0f, -9.0f)) {
        client.stop();
        return 18; // a frame after iq_stop must not re-arm this subscription
    }
    releaseFixture("stop-checked");

    StreamMetadata reconnected{};
    if (!waitForMetadata(requestedRate, 14076000, std::chrono::seconds(3), reconnected, true) ||
        reconnected.epoch <= changedRead.metadata.epoch ||
        !client.waitForSamples(samples.size(), std::chrono::seconds(2))) {
        client.stop();
        return 19;
    }
    const auto reconnectRead = client.popWithMetadata(samples);
    client.stop();
    if (reconnectRead.samplesCopied != samples.size() ||
        reconnectRead.metadata.sampleRateHz != requestedRate ||
        reconnectRead.metadata.panCenterHz != 14076000 ||
        reconnectRead.metadata.epoch != reconnected.epoch ||
        samples[0] != std::complex<float>(-0.5f, 0.5f) ||
        samples[1] != std::complex<float>(0.75f, -0.75f) ||
        samples[2] != std::complex<float>(0.125f, -0.125f)) return 20;

    diagnostics = client.diagnostics();
    if (diagnostics.overflowDroppedSamplePairs != 2 ||
        diagnostics.metadataEpochFlushedSamplePairs != 2) {
        return 21; // counts persist across automatic reconnect and stop cleanup
    }
    if (!client.start(config)) return 22;
    diagnostics = client.diagnostics();
    if (diagnostics.overflowDroppedSamplePairs != 0 ||
        diagnostics.metadataEpochFlushedSamplePairs != 0) {
        client.stop();
        return 23; // an explicit new client session starts fresh counters
    }
    client.stop();

    std::cout << "fake TCI no-echo activation, negative/stale-frame, lifecycle, and metadata-epoch checks passed at "
              << requestedRate << " -> " << changedRate << " samples/s\n";
    return 0;
}
