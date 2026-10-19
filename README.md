# CyberEther TCI IQ Source

**Status: prototype; synthetic checks and CyberEther 1.15.0 plugin loading are verified, but live TCI and full graph execution are not.** The repository contains a receive-only TCI client, defensive control/IQ parsing, bounded buffering, a CyberEther source block, synthetic tests, and an example flowgraph. The macOS arm64 plugin compiles against CyberEther 1.15.0, packages as a `.cep`, and loads through the real SDK plugin loader with its source module registered. No AetherSDR server or radio was used.

This source is intended to receive IQ from AetherSDR's TCI server while AetherSDR is connected to a FLEX-6400. It does not transmit or key PTT. Its outbound TCI commands are limited to `iq_samplerate`, `iq_start`, and `iq_stop`; it has no generic command sender. Tune in AetherSDR. The current Aether `dds` SET behavior must not be treated as a panadapter retune.

## Architecture and initial scope

- The source block targets a `ws://` endpoint at `127.0.0.1:50001` by default. The client accepts only literal IPv4 loopback `127.0.0.1`; it does not resolve hostnames or connect to remote hosts. TCP connect is nonblocking, polls cancellation every 50 ms, and has a three-second timeout.
- The client requests one of the FLEX-6400 IQ rates: **24, 48, or 96 kS/s**. AetherSDR applies `iq_samplerate` globally to TCI IQ streams and announces the achieved rate to connected peers, so changing it may affect other TCI IQ clients. The source waits for the server's `iq_samplerate` echo and `dds:<receiver>,<pan-center-Hz>;` state before publishing actual rate and pan-center metadata; a request alone is not treated as confirmation.
- The parser uses AetherSDR's 64-byte, 16-word TCI IQ header followed by interleaved little-endian float32 I/Q values. It validates the receiver, rate, format, codec, CRC field, type, channel count, scalar count, reserved words, exact frame length, and finite sample values before producing CF32 samples.
- The receive queue is bounded and drops oldest samples on overflow. The source waits for a complete output buffer; a short read is not padded with zeros. A rate or pan-center update increments the metadata epoch and clears queued samples under the same lock used to pair output with metadata. Disconnect clears queued samples, and the client reconnects with backoff.
- `examples/tci-iq-spectrum.yml` connects the source directly to a spectrum analyzer, without a fixed-rate throttle. The source is designed to pace graph computation at the confirmed server rate. The frequency attribute follows AetherSDR's reported panadapter center. This flowgraph has not been qualified in the real CyberEther host.

The plugin scaffold targets CyberEther 1.15.0 or newer, macOS arm64, and the official `.cep` manifest shape. CyberEther's plugin ABI is experimental.

## Verification status

Synthetic checks run locally with strict-warning Clang C++20 builds and `-DNDEBUG`:

- Protocol fixtures cover the observed IQ header, sample decoding, supported rates, metadata parsing, malformed frames, and all-or-nothing queue reads.
- A loopback fake TCI server covers the receive-only command sequence, 24/48/96 kS/s scenarios, rate/center metadata epochs, queue clearing, full-buffer readiness, disconnect, and reconnect.
- Dedicated checks cover a disconnected-peer send, cancellation during a stalled handshake, deterministic pending-connect deadline expiry/cancellation, and protocol test failures remaining enabled under `NDEBUG`.
- A Release CMake build and all seven synthetic tests pass, including loopback-only fake-server and pending-connect deadline/cancellation checks. Python syntax checks also pass.
- The plugin was compiled against the isolated CyberEther v1.15.0 SDK, packaged with its compiled macOS arm64 target, loaded by the actual CyberEther plugin loader, and verified to register the `tci_iq_source` module.
- The block contract harness separately calls the source block's `define()` and `create()` methods against a **fake Jetstream API**. It checks configuration descriptor types and mapping. The actual host graph was not executed: the isolated headless CyberEther build reports no viewport backend.

No AetherSDR server or radio was used for these checks.

## Native build and use

The native build was verified on macOS arm64 against an isolated build of CyberEther v1.15.0 from its official source tag. The plugin compiles and packages as `cyberether_tci_iq_source.cep`; the real CyberEther plugin loader loads it and reports the `tci_iq_source` module. This only verified plugin loading and module registration. A headless host graph could not be executed because the isolated CyberEther build has no viewport backend, and live AetherSDR/FLEX qualification remains a separately gated step.

To use the example after review and a real host integration check, enable AetherSDR's TCI server yourself and confirm its listener exposure is acceptable. Then open the bundled TCI IQ Spectrum example and keep tuning in AetherSDR. This project does not enable TCI or change firewall/server settings. Starting the IQ subscription may cause AetherSDR to allocate or bind a DAX IQ channel for the selected receiver.

To use the example after a real build, enable AetherSDR's TCI server yourself and confirm its listener exposure is acceptable. Then open the bundled TCI IQ Spectrum example and keep tuning in AetherSDR. This project does not enable TCI or change firewall/server settings. Starting the IQ subscription may cause AetherSDR to allocate or bind a DAX IQ channel for the selected receiver.

## TCI server exposure

AetherSDR's TCI documentation says its server listens on every network interface without a password. A client connecting to `127.0.0.1` does not limit which other machines can reach the server. A reachable TCI server supports radio-control commands, including tune and PTT, even though this plugin itself has a strict receive-only command set. Review server exposure and user-side network protections before enabling TCI.

## Sources

- [CyberEther plugin blueprint, ABI, and `.cep` packaging](https://cyberether.org/docs/plugins)
- [CyberEther 1.15.0 plugin example](https://github.com/luigifcruz/CyberEther/tree/v1.15.0/examples/plugins/blueprint)
- [AetherSDR TCI server documentation](https://docs.aethersdr.com/tci-server)
- [AetherSDR TCI IQ receiver mapping](https://github.com/aethersdr/AetherSDR/blob/main/docs/architecture/tci-receivers.md)
- [AetherSDR IQ frame header](https://github.com/aethersdr/AetherSDR/blob/main/src/core/TciAudioHeader.h)
- [AetherSDR TCI server implementation](https://github.com/aethersdr/AetherSDR/blob/main/src/core/TciServer.cpp)
- [AetherSDR FLEX DAX IQ rates](https://github.com/aethersdr/AetherSDR/blob/main/docs/user/docs/dax-iq-streaming.md)
- [Optional later reference: hb9fxq/flexlib-go](https://github.com/hb9fxq/flexlib-go) — IQ extraction/CGO backing only; not a complete SoapySDR driver and not a dependency.

Track remaining work in the [GitHub issues](https://github.com/zarthur/cyberether-tci/issues).
