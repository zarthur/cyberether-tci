# CyberEther TCI IQ Source

**Status: prototype; synthetic tests, CyberEther 1.15.0 plugin loading, and a live receive-only spectrum/waterfall check have passed.** The CyberEther plugin ABI is experimental. This source is for AetherSDR TCI IQ from a FLEX-6400 on macOS Apple Silicon; it does not transmit, key PTT, or tune the radio.

## Scope and behavior

- Connects only to literal IPv4 loopback `127.0.0.1`, port `50001` by default. It does not resolve names or connect to remote hosts.
- Requests FLEX-6400 DAX IQ rates of 24, 48, or 96 kS/s. The achieved `iq_samplerate` and `dds` pan-center metadata from the server determine the output stream metadata; a request alone is not treated as confirmation. Keep tuning in AetherSDR. Its current `dds` SET command acknowledges without retuning the pan center, so plugin tuning is out of scope.
- Sends only `iq_samplerate`, `iq_start`, and `iq_stop`; there is no generic TCI command sender.
- Parses AetherSDR's 64-byte TCI IQ header and interleaved little-endian float32 I/Q. It validates receiver, negotiated rate, format, codec, CRC field, type, channel count, scalar count, reserved words, frame length, and finite samples before producing CF32.
- Uses a bounded queue, drops oldest data on overflow, waits for complete output buffers, clears samples across metadata epochs/disconnects, and reconnects with backoff.
- `examples/tci-iq-spectrum.yml` connects the source to CyberEther's spectrum analyzer and waterfall.

## Build and package

Build against the CyberEther 1.15.0 development SDK on macOS arm64. Set `PKG_CONFIG_PATH` to that SDK's `lib/pkgconfig` directory:

```sh
PKG_CONFIG_PATH=/path/to/cyberether-1.15/lib/pkgconfig meson setup build .
meson compile -C build cyberether_tci_iq_source_cep
```

The `.cep` packager stages a copy of the plugin, rewrites the SDK's `libjetstream.dylib` reference to `@rpath/libjetstream.dylib`, and adds `@executable_path` so the loader resolves Jetstream from `CyberEther.app/Contents/MacOS`. The source build binary is left unchanged. The USTAR and gzip archive headers use normalized timestamps, so packaging identical inputs produces identical `.cep` bytes. Packaging requires macOS `otool` and `install_name_tool`.

## Install, start, and stop

1. Copy `build/cyberether_tci_iq_source.cep` to `~/Documents/CyberEtherPlugins/cyberether_tci_iq_source.cep`, replacing an older copy if present, then restart CyberEther 1.15.0 so its plugin registry discovers the source block.
2. In AetherSDR, enable its TCI server yourself and confirm its network exposure is acceptable. The source defaults to `127.0.0.1:50001`, receiver 0, and 48 kS/s. Keep tuning in AetherSDR.
3. In CyberEther, choose **Examples** from the home screen and open **AetherSDR TCI IQ Spectrum**. The example starts the flowgraph and displays the spectrum and waterfall when the TCI stream is available.
4. Stop the flowgraph with its stop control or close the flowgraph. If prompted to save the example, choose **Don't Save**. Closing the graph sends `iq_stop`; reopening it starts a new IQ subscription.

## Verification

- The Release CMake suite covers protocol parsing, synthetic fixtures, bounded buffering, cancellation/deadlines, block configuration, and loopback disconnect/reconnect. Packaging tests check host-library relocation and byte-identical archive output for identical inputs.
- The macOS arm64 `.cep` was built against the isolated CyberEther 1.15.0 SDK, loaded by the real plugin loader, and registered the `tci_iq_source` module. The normal CyberEther 1.15.0 app displayed the example's spectrum/waterfall while receiving AetherSDR TCI IQ from the connected FLEX-6400 at 48 kS/s and a 14.074 MHz pan center. Aether's diagnostic log showed DAX IQ stream allocation/removal, `iq_stop`, zero remaining TCI clients, and PTT false.
- Reconciled Aether log timing: the long visual session began with DAX IQ creation at 11:57:10.643 and disconnected at 12:23:15.131 with `connectionAgeMs=1564531`, `lastRxCommand=iq_stop`, and PTT false. A reconnect at 12:23:17.740 ended at 12:23:27.041 after 9,334 ms, also with `iq_stop`, zero clients, and PTT false. These records confirm that the long session was live Aether TCI/DAX, not a loopback fixture.
- A later bounded visual run began at 12:59:46.200 and disconnected at 13:00:05.504 (`connectionAgeMs=19336`). The server recorded `lastRxCommand=iq_stop`, removed DAX stream `0x20000000`, reported zero TCI clients, and reported PTT false. An independent 180-second watchdog started at 12:59:32; it observed the stop marker at 13:01:38 and exited without signaling CyberEther. Afterward, `lsof` showed the Aether listener still present and no established client connections.
- The graph was closed after qualification. The TCI server remains enabled at the owner's request; no firewall settings were changed. No tune or PTT command was sent by the plugin.

## TCI server exposure

AetherSDR's TCI server listens on all network interfaces without a password. A client connecting to localhost does not restrict other machines that can reach the server. The server supports radio-control commands, including tune and PTT, even though this plugin's own command allowlist is receive-only. Review exposure before enabling the server. This project does not enable TCI or change firewall settings.

## References

- [CyberEther plugin blueprint, ABI, and `.cep` packaging](https://cyberether.org/docs/plugins)
- [CyberEther 1.15.0 plugin example](https://github.com/luigifcruz/CyberEther/tree/v1.15.0/examples/plugins/blueprint)
- [AetherSDR TCI server documentation](https://docs.aethersdr.com/tci-server)
- [AetherSDR receiver mapping](https://github.com/aethersdr/AetherSDR/blob/main/docs/architecture/tci-receivers.md)
- [AetherSDR IQ header](https://github.com/aethersdr/AetherSDR/blob/main/src/core/TciAudioHeader.h)
- [AetherSDR TCI server implementation](https://github.com/aethersdr/AetherSDR/blob/main/src/core/TciServer.cpp)
- [FLEX DAX IQ rates](https://github.com/aethersdr/AetherSDR/blob/main/docs/user/docs/dax-iq-streaming.md)
- [Optional later reference: `hb9fxq/flexlib-go`](https://github.com/hb9fxq/flexlib-go) — IQ extraction/CGO backing only; not a complete SoapySDR driver or a dependency.
- [Development tracking issues](https://github.com/zarthur/cyberether-tci/issues)
