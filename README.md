# CyberEther TCI IQ Source

**Status: planning only. Nothing in this repository works yet; no plugin has been implemented or qualified.**

This project plans a receive-only CyberEther source plugin for macOS on Apple Silicon. It will receive IQ from AetherSDR's TCI server while AetherSDR is connected to a FLEX-6400.

## Planned design

The plugin will be packaged as a CyberEther `.cep` bundle containing a macOS arm64 plugin library and an example flowgraph. Its source block will connect to AetherSDR over WebSocket, parse confirmed TCI control/metadata and binary IQ frames, and produce CyberEther `CF32` complex samples. The implementation must confirm protocol fields and binary layout from AetherSDR's implementation or reliable fixtures; it must not infer undocumented fields.

The initial source follows the actual panadapter center reported by `dds` and the actual IQ sample rate reported by `iq_samplerate`. Tuning stays in AetherSDR: the source will not promise plugin tuning or issue a `dds` SET as a retune mechanism. The receive-only command allowlist excludes transmit, PTT, tune, and other radio-control commands.

For a FLEX-6400, planned IQ rates are **24, 48, and 96 kS/s**; 192 kS/s is not supported on that model. The plugin will use the negotiated/reported rate rather than assume a request succeeded.

## TCI exposure

AetherSDR's TCI documentation says its server listens on every network interface without a password. Connecting this plugin to `127.0.0.1` does not limit which other machines can reach the server. Setup guidance must make that exposure clear and review appropriate user-side protections. This project will not enable the TCI server or change firewall/server settings automatically.

## References

- [CyberEther plugin blueprint, ABI, and `.cep` packaging](https://cyberether.org/docs/plugins)
- [AetherSDR TCI server and protocol documentation](https://docs.aethersdr.com/tci-server)
- [AetherSDR DAX IQ streaming and model rate limits](https://github.com/aethersdr/AetherSDR/blob/main/docs/user/docs/dax-iq-streaming.md)
- [Optional later reference: hb9fxq/flexlib-go](https://github.com/hb9fxq/flexlib-go) — an IQ extraction/CGO backing reference, not a planned dependency or complete SoapySDR driver.

The plugin ABI and bundle format are experimental. The initial build target is CyberEther 1.15.0 on macOS arm64; version and ABI compatibility must be pinned and verified during the scaffold work.

See [ISSUES.md](ISSUES.md) for the proposed development tracker. These are local drafts until a private GitHub repository is available.
