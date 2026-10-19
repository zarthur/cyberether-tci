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
    return Result::SUCCESS;
}

Result TciIqSourceBlockImpl::create() {
    JST_CHECK(moduleCreate("tci_iq_source", moduleConfig, {}));
    JST_CHECK(moduleExposeOutput("signal", {"tci_iq_source", "signal"}));
    return Result::SUCCESS;
}

JST_REGISTER_BLOCK(TciIqSourceBlockImpl, {"tci_iq_source"});

}  // namespace Jetstream::Blocks
