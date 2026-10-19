# CyberEther TCI IQ Source

**Status: early implementation; not yet packaged or qualified in CyberEther.** The repository now contains the receive-only WebSocket client, defensive TCI control/IQ parsing, bounded sample buffering, a CyberEther 1.15.0 plugin scaffold, synthetic fixtures, and an example spectrum flowgraph. The parser and client pass local synthetic tests. The `.cep` build and integration with CyberEther 1.15.0 remain unverified.

This plugin receives IQ from AetherSDR's TCI server while AetherSDR is connected to a FLEX-6400. It does not transmit or key PTT. The client only sends `iq_samplerate`, `iq_start`, and `iq_stop`; it has no generic TCI command sender. Tune in AetherSDR. Aether's current `dds` SET behavior must not be treated as a retune operation.

## Architecture and current scope

- The source block creates a CF32 stream and connects to a `ws://` TCI endpoint (default `127.0.0.1:50001`).
- The client requests one of the FLEX-6400 supported IQ rates: **24, 48, or 96 kS/s**. AetherSDR applies `iq_samplerate` globally to TCI IQ streams and announces the achieved rate to connected peers, so changing it can affect other TCI IQ clients. This source waits for the server's `iq_samplerate` echo and `dds:<receiver>,<pan-center-Hz>;` state before publishing actual sample-rate and pan-center metadata. It does not assume the requested rate was accepted.
- Binary IQ frames use AetherSDR's observed 64-byte, 16-word TCI header followed by interleaved little-endian float32 I/Q values. The parser checks receiver, rate, format, codec, CRC field, type, channel count, scalar count, reserved words, exact frame length, and finite sample values before creating CF32 samples.
- The receive queue is bounded. On overflow it drops the oldest samples; shortages are zero-filled. Disconnect clears queued data and the client reconnects with backoff. It requests the same receive-only stream after reconnect.
- `examples/tci-iq-spectrum.yml` routes 2048 samples at the default 48 kS/s rate through a 43 ms throttle to a spectrum analyzer, keeping display updates close to real time. If changing the requested rate or block size, adjust `intervalMs` to approximately `bufferSize * 1000 / actual_sample_rate`. The frequency attribute follows Aether's actual pan center so downstream displays can place the sampled band correctly.

The protocol implementation is isolated in `src/tci/` and can be checked without the CyberEther SDK. `CMakeLists.txt` builds the synthetic protocol and local fake-WebSocket tests. `meson.build` targets CyberEther 1.15.0 or newer, macOS arm64, and the official `.cep` manifest shape; it still needs a real 1.15.0 build/loader check.

## Synthetic verification

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

These tests use synthetic binary frames and a fake WebSocket server bound to loopback. They do not connect to AetherSDR or radio hardware.

## Build and try the plugin

Build against the official CyberEther 1.15.0 development dependency using Meson, then compile the `cyberether_tci_iq_source.cep` target. On this version, the plugin ABI is experimental. The package minimum is pinned to 1.15.0 and the initial target is macOS arm64.

To use the example, first enable AetherSDR's TCI server yourself and confirm its listener exposure is acceptable. Then open the bundled TCI IQ Spectrum example and keep tuning in AetherSDR. This project does not enable TCI or adjust firewall/server settings. The block starts an IQ subscription, which may cause AetherSDR to allocate or bind a DAX IQ channel for the selected receiver.

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
