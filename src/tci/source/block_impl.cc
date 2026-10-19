#include <cyberether_tci/source/block.hh>
#include <cyberether_tci/source/module.hh>
#include <jetstream/detail/block_impl.hh>

namespace Jetstream::Blocks {

struct TciIqSourceBlockImpl : public Block::Impl, public DynamicConfig<TciIqSource> {
    Result configure() override;
    Result define() override;
    Result create() override;

  protected:
    std::shared_ptr<Modules::TciIqSource> moduleConfig = std::make_shared<Modules::TciIqSource>();
};

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
    JST_CHECK(defineInterfaceConfig("host", "TCI Host", "AetherSDR TCI server host.",
                                    {{"type", "string"}}));
    JST_CHECK(defineInterfaceConfig("port", "TCI Port", "AetherSDR TCI WebSocket port.",
                                    {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}));
    JST_CHECK(defineInterfaceConfig("receiver", "Receiver", "TCI receiver number.",
                                    {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}));
    JST_CHECK(defineInterfaceConfig("sampleRateHz", "Requested IQ Rate", "AetherSDR IQ rate request.",
                                    {{"type", "integer"}, {"enum", {24000, 48000, 96000}}}));
    JST_CHECK(defineInterfaceConfig("bufferSize", "Block Size", "CF32 samples per output buffer.",
                                    {{"type", "integer"}, {"minimum", 256}, {"maximum", 65536}}));
    return Result::SUCCESS;
}

Result TciIqSourceBlockImpl::create() {
    JST_CHECK(moduleCreate("tci_iq_source", moduleConfig, {}));
    JST_CHECK(moduleExposeOutput("signal", {"tci_iq_source", "signal"}));
    return Result::SUCCESS;
}

JST_REGISTER_BLOCK(TciIqSourceBlockImpl, {"tci_iq_source"});

}  // namespace Jetstream::Blocks
