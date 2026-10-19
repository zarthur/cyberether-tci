#include <cyberether_tci/client.hpp>
#include <cyberether_tci/source/block.hh>
#include <cyberether_tci/source/module.hh>
#include <jetstream/detail/block_impl.hh>

#include <any>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace Jetstream::Blocks {

struct TciIqSourceBlockImpl : public Block::Impl, public DynamicConfig<TciIqSource> {
    ~TciIqSourceBlockImpl() override;
    Result configure() override;
    Result define() override;
    Result create() override;

  protected:
    std::shared_ptr<Modules::TciIqSource> moduleConfig = std::make_shared<Modules::TciIqSource>();
    std::shared_ptr<cyberether_tci::ClientDiagnosticCounters> diagnostics_;
    U64 diagnosticsId_ = 0;
};

TciIqSourceBlockImpl::~TciIqSourceBlockImpl() {
    if (diagnosticsId_ != 0) cyberether_tci::unregisterClientDiagnostics(diagnosticsId_);
}

Result TciIqSourceBlockImpl::configure() {
    moduleConfig->host = host;
    moduleConfig->port = port;
    moduleConfig->receiver = receiver;
    moduleConfig->sampleRateHz = sampleRateHz;
    moduleConfig->bufferSize = bufferSize;
    return Result::SUCCESS;
}

Result TciIqSourceBlockImpl::define() {
    JST_CHECK(defineInterfaceOutput("signal", "Signal", "Complex float IQ samples."));
    JST_CHECK(defineInterfaceConfig("host", "TCI Host", "Literal 127.0.0.1 only; hostnames and remote endpoints are not supported.",
                                    {{"type", "text"}}));
    JST_CHECK(defineInterfaceConfig("port", "TCI Port", "AetherSDR TCI WebSocket port.",
                                    {{"type", "uint"}}));
    JST_CHECK(defineInterfaceConfig("receiver", "Receiver", "TCI receiver number.",
                                    {{"type", "uint"}}));
    JST_CHECK(defineInterfaceConfig(
        "sampleRateHz", "Requested IQ Rate", "AetherSDR IQ rate request.",
        {{"type", "dropdown"}, {"options", Parser::Sequence{
            Parser::Map{{"label", "24 kS/s"}, {"value", "24000"}},
            Parser::Map{{"label", "48 kS/s"}, {"value", "48000"}},
            Parser::Map{{"label", "96 kS/s"}, {"value", "96000"}},
        }}}));
    JST_CHECK(defineInterfaceConfig("bufferSize", "Block Size", "CF32 samples per output buffer.",
                                    {{"type", "uint"}}));
    JST_CHECK(defineInterfaceMetric(
        "overflowDroppedSamples", "Overflow drops",
        "Cumulative CF32 sample pairs discarded because the receive queue was full.",
        {{"type", "label"}}, [this]() -> std::any {
            const auto counters = std::atomic_load_explicit(&diagnostics_, std::memory_order_acquire);
            if (!counters) return std::string("No TCI session");
            return std::to_string(counters->overflowDroppedSamplePairs.load(std::memory_order_relaxed)) +
                   " sample pairs";
        }));
    JST_CHECK(defineInterfaceMetric(
        "metadataEpochFlushedSamples", "Metadata flushes",
        "Cumulative queued CF32 sample pairs intentionally cleared when confirmed rate or pan-center metadata changed.",
        {{"type", "label"}}, [this]() -> std::any {
            const auto counters = std::atomic_load_explicit(&diagnostics_, std::memory_order_acquire);
            if (!counters) return std::string("No TCI session");
            return std::to_string(counters->metadataEpochFlushedSamplePairs.load(std::memory_order_relaxed)) +
                   " sample pairs";
        }));
    return Result::SUCCESS;
}

Result TciIqSourceBlockImpl::create() {
    if (diagnosticsId_ != 0) cyberether_tci::unregisterClientDiagnostics(diagnosticsId_);
    auto counters = std::make_shared<cyberether_tci::ClientDiagnosticCounters>();
    diagnosticsId_ = cyberether_tci::registerClientDiagnostics(counters);
    std::atomic_store_explicit(&diagnostics_, std::move(counters), std::memory_order_release);
    moduleConfig->diagnosticsId = diagnosticsId_;
    JST_CHECK(moduleCreate("tci_iq_source", moduleConfig, {}));
    JST_CHECK(moduleExposeOutput("signal", {"tci_iq_source", "signal"}));
    return Result::SUCCESS;
}

JST_REGISTER_BLOCK(TciIqSourceBlockImpl, {"tci_iq_source"});

}  // namespace Jetstream::Blocks
