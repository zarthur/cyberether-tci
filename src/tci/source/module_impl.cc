#include "module_impl.hh"

#include <algorithm>
#include <chrono>
#include <thread>

#include <jetstream/memory/axis.hh>

namespace Jetstream::Modules {

Result TciIqSourceImpl::validate() {
    const auto& config = *candidate();
    if (config.host.empty() || config.host.find_first_of("\r\n") != std::string::npos ||
        config.port == 0 || config.port > 65535 || config.receiver > 3 ||
        !cyberether_tci::isFlex6400IqRate(config.sampleRateHz) ||
        config.bufferSize < 256 || config.bufferSize > 65536) {
        JST_ERROR("[TCI_IQ_SOURCE] Invalid endpoint, receiver, sample rate, or buffer size.");
        return Result::ERROR;
    }
    return Result::SUCCESS;
}

Result TciIqSourceImpl::define() {
    JST_CHECK(defineInterfaceOutput("signal"));
    return Result::SUCCESS;
}

Result TciIqSourceImpl::create() {
    const auto& config = *candidate();
    JST_CHECK(signal.create(device(), DataType::CF32, {config.bufferSize}));
    JST_CHECK(SetSignalAxes(signal, {.sample = Index{0}}));
    outputs()["signal"].produced(name(), "signal", signal);

    cyberether_tci::ClientConfig clientConfig;
    clientConfig.host = config.host;
    clientConfig.port = static_cast<std::uint16_t>(config.port);
    clientConfig.receiver = config.receiver;
    clientConfig.requestedRateHz = config.sampleRateHz;
    clientConfig.queueCapacity = std::max<std::size_t>(config.bufferSize * 32ULL, 8192);
    if (!client.start(std::move(clientConfig))) {
        JST_ERROR("[TCI_IQ_SOURCE] Could not start the TCI receive client.");
        return Result::ERROR;
    }

    // Do not advertise the requested rate as if it were negotiated. Wait for
    // the first actual TCI state burst; if the server is not ready, destroy
    // the graph and let the user start it after enabling TCI.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto metadata = client.metadata();
        if (metadata.ready && metadata.iqStarted && metadata.sampleRateHz != 0 && metadata.panCenterHz != 0) {
            lastSampleRateHz = metadata.sampleRateHz;
            lastCenterHz = metadata.panCenterHz;
            JST_CHECK(signal.setAttribute("sampleRate", static_cast<F32>(lastSampleRateHz)));
            JST_CHECK(signal.setAttribute("frequency", static_cast<F32>(lastCenterHz)));
            return Result::SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    client.stop();
    JST_ERROR("[TCI_IQ_SOURCE] No ready IQ stream and confirmed dds/iq_samplerate state within 5 seconds.");
    return Result::ERROR;
}

Result TciIqSourceImpl::destroy() {
    client.stop();
    return Result::SUCCESS;
}

Result TciIqSourceImpl::reconfigure() {
    const auto& config = *candidate();
    if (config.host != host || config.port != port || config.receiver != receiver ||
        config.sampleRateHz != sampleRateHz || config.bufferSize != bufferSize) {
        return Result::RECREATE;
    }
    return Result::SUCCESS;
}

}  // namespace Jetstream::Modules
