#pragma once

#include <jetstream/block.hh>
#include <jetstream/types.hh>

#include <string>

namespace Jetstream::Blocks {

struct TciIqSource : public Block::Config {
    std::string host = "127.0.0.1";
    U32 port = 50001;
    U32 receiver = 0;
    U32 sampleRateHz = 48000;
    U32 bufferSize = 8192;

    JST_BLOCK_TYPE(tci_iq_source);
    JST_BLOCK_DOMAIN("Sources");
    JST_BLOCK_PARAMS(host, port, receiver, sampleRateHz, bufferSize);
    JST_BLOCK_DESCRIPTION(
        "AetherSDR TCI IQ Source",
        "Receives CF32 IQ samples from an AetherSDR TCI server.",
        "# AetherSDR TCI IQ Source\n"
        "Receives one TCI IQ receiver as CF32 samples. This block is receive-only: "
        "it sends only iq_samplerate, iq_start, and iq_stop records. Tuning stays "
        "in AetherSDR; the block follows dds and iq_samplerate state reported by "
        "the server.\n\n"
        "The host defaults to 127.0.0.1 and the server must already be enabled. "
        "AetherSDR's TCI listener may be reachable on every network interface, "
        "without authentication, even when this client uses localhost.\n\n"
        "## Configuration\n"
        "- Host and port: AetherSDR TCI WebSocket endpoint (ws:// only).\n"
        "- Receiver: TCI receiver 0 through 3.\n"
        "- Sample rate: request 24, 48, or 96 kS/s. AetherSDR applies this rate "
        "globally to TCI IQ streams, so other TCI IQ clients may be affected. The "
        "output uses the server's confirmed iq_samplerate value.\n"
        "- Buffer size: CF32 samples emitted per compute cycle."
    );
};

}  // namespace Jetstream::Blocks
