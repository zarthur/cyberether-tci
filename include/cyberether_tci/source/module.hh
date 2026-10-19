#pragma once

#include <jetstream/module.hh>
#include <jetstream/types.hh>

#include <string>

namespace Jetstream::Modules {

struct TciIqSource : public Module::Config {
    std::string host = "127.0.0.1";
    U32 port = 50001;
    U32 receiver = 0;
    U32 sampleRateHz = 48000;
    U32 bufferSize = 8192;
    U64 diagnosticsId = 0;

    JST_MODULE_TYPE(tci_iq_source);
    JST_MODULE_PARAMS(host, port, receiver, sampleRateHz, bufferSize, diagnosticsId);
};

}  // namespace Jetstream::Modules
