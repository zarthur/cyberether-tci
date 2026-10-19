#include "cyberether_tci/protocol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace cyberether_tci;

static void putLe32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = std::byte((value >> (8 * i)) & 0xffU);
}

static std::vector<std::byte> makeIqFixture(std::uint32_t receiver = 0,
                                             std::uint32_t rate = 48000) {
    std::vector<std::byte> bytes(kTciAudioHeaderBytes + 4 * sizeof(float));
    putLe32(bytes, 0, receiver);
    putLe32(bytes, 4, rate);
    putLe32(bytes, 8, 3); // float32
    putLe32(bytes, 12, 0); // uncompressed
    putLe32(bytes, 16, 0); // unused CRC
    putLe32(bytes, 20, 4); // scalar real samples, two I/Q pairs
    putLe32(bytes, 24, 0); // IQ
    putLe32(bytes, 28, 2); // I and Q channels
    const std::array<float, 4> values{0.25f, -0.5f, 1.0f, 0.125f};
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &values[i], sizeof(bits));
        putLe32(bytes, kTciAudioHeaderBytes + 4 * i, bits);
    }
    return bytes;
}

int main() {
    assert(isFlex6400IqRate(24000));
    assert(isFlex6400IqRate(48000));
    assert(isFlex6400IqRate(96000));
    assert(!isFlex6400IqRate(192000));

    ControlParser parser;
    std::vector<ControlUpdate> updates;
    assert(parser.append("ready;dds:0,14", updates));
    assert(parser.append("074000;iq_samplerate:48000;iq_start:0;", updates));
    assert(updates.size() == 4);
    assert(updates[0].kind == ControlUpdate::Kind::Ready);
    assert(updates[1].kind == ControlUpdate::Kind::PanCenterHz);
    assert(updates[1].receiver == 0 && updates[1].value == 14074000);
    assert(updates[2].kind == ControlUpdate::Kind::SampleRateHz && updates[2].value == 48000);
    assert(updates[3].kind == ControlUpdate::Kind::IqStarted);

    ControlParser badRate;
    std::vector<ControlUpdate> ignored;
    assert(!badRate.append("iq_samplerate:192000;", ignored));
    ControlParser badCenter;
    assert(!badCenter.append("dds:4,14074000;", ignored));
    ControlParser oversized;
    assert(!oversized.append(std::string(kMaxControlRecordBytes + 1, 'x'), ignored));

    const auto fixture = makeIqFixture();
    const auto decoded = parseIqFrame(fixture, 0);
    assert(decoded && decoded->sampleRateHz == 48000 && decoded->samples.size() == 2);
    assert(decoded->samples[0] == std::complex<float>(0.25f, -0.5f));
    assert(decoded->samples[1] == std::complex<float>(1.0f, 0.125f));
    assert(!parseIqFrame(fixture, 1));
    assert(!parseIqFrame(std::span(fixture).first(63), 0));
    assert(!parseIqFrame(std::span(fixture).first(fixture.size() - 1), 0));

    auto wrongFormat = fixture;
    putLe32(wrongFormat, 8, 2);
    assert(!parseIqFrame(wrongFormat, 0));
    auto wrongType = fixture;
    putLe32(wrongType, 24, 1);
    assert(!parseIqFrame(wrongType, 0));
    auto wrongChannels = fixture;
    putLe32(wrongChannels, 28, 1);
    assert(!parseIqFrame(wrongChannels, 0));
    auto reserved = fixture;
    putLe32(reserved, 32, 1);
    assert(!parseIqFrame(reserved, 0));
    auto badLength = fixture;
    putLe32(badLength, 20, 0xffffffffU);
    assert(!parseIqFrame(badLength, 0));
    auto oddScalarCount = fixture;
    putLe32(oddScalarCount, 20, 3);
    assert(!parseIqFrame(oddScalarCount, 0));
    auto badFrameRate = fixture;
    putLe32(badFrameRate, 4, 192000);
    assert(!parseIqFrame(badFrameRate, 0));
    auto nonfinite = fixture;
    putLe32(nonfinite, kTciAudioHeaderBytes, 0x7f800000U);
    assert(!parseIqFrame(nonfinite, 0));

    BoundedIqQueue queue(3);
    queue.push(*decoded); // 2 samples
    queue.push(*decoded); // overflow drops the oldest sample
    std::array<std::complex<float>, 4> output{};
    const auto copied = queue.pop(output);
    assert(copied == 3 && queue.size() == 0);
    assert(output[0] == decoded->samples[1]);
    assert(output[1] == decoded->samples[0]);
    assert(output[2] == decoded->samples[1]);
    assert(output[3] == std::complex<float>{}); // underflow is zero-filled
    queue.clear();

    std::cout << "synthetic TCI protocol fixtures passed\n";
    return 0;
}
