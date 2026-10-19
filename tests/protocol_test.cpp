#include "cyberether_tci/protocol.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

using namespace cyberether_tci;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition << '\n'; \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

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

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--verify-check-failure") CHECK(false);
    CHECK(argc == 1);
    CHECK(isFlex6400IqRate(24000));
    CHECK(isFlex6400IqRate(48000));
    CHECK(isFlex6400IqRate(96000));
    CHECK(!isFlex6400IqRate(192000));

    ControlParser parser;
    std::vector<ControlUpdate> updates;
    CHECK(parser.append("ready;dds:0,14", updates));
    CHECK(parser.append("074000;iq_samplerate:48000;iq_start:0;", updates));
    CHECK(updates.size() == 4);
    CHECK(updates[0].kind == ControlUpdate::Kind::Ready);
    CHECK(updates[1].kind == ControlUpdate::Kind::PanCenterHz);
    CHECK(updates[1].receiver == 0 && updates[1].value == 14074000);
    CHECK(updates[2].kind == ControlUpdate::Kind::SampleRateHz && updates[2].value == 48000);
    CHECK(updates[3].kind == ControlUpdate::Kind::IqStarted);

    ControlParser badRate;
    std::vector<ControlUpdate> ignored;
    CHECK(!badRate.append("iq_samplerate:192000;", ignored));
    ControlParser badCenter;
    CHECK(!badCenter.append("dds:4,14074000;", ignored));
    ControlParser oversized;
    CHECK(!oversized.append(std::string(kMaxControlRecordBytes + 1, 'x'), ignored));

    const auto fixture = makeIqFixture();
    const auto decoded = parseIqFrame(fixture, 0);
    CHECK(decoded && decoded->sampleRateHz == 48000 && decoded->samples.size() == 2);
    CHECK(decoded->samples[0] == std::complex<float>(0.25f, -0.5f));
    CHECK(decoded->samples[1] == std::complex<float>(1.0f, 0.125f));
    CHECK(!parseIqFrame(fixture, 1));
    CHECK(!parseIqFrame(std::span(fixture).first(63), 0));
    CHECK(!parseIqFrame(std::span(fixture).first(fixture.size() - 1), 0));

    auto wrongFormat = fixture;
    putLe32(wrongFormat, 8, 2);
    CHECK(!parseIqFrame(wrongFormat, 0));
    auto wrongType = fixture;
    putLe32(wrongType, 24, 1);
    CHECK(!parseIqFrame(wrongType, 0));
    auto wrongChannels = fixture;
    putLe32(wrongChannels, 28, 1);
    CHECK(!parseIqFrame(wrongChannels, 0));
    auto reserved = fixture;
    putLe32(reserved, 32, 1);
    CHECK(!parseIqFrame(reserved, 0));
    auto badLength = fixture;
    putLe32(badLength, 20, 0xffffffffU);
    CHECK(!parseIqFrame(badLength, 0));
    auto oddScalarCount = fixture;
    putLe32(oddScalarCount, 20, 3);
    CHECK(!parseIqFrame(oddScalarCount, 0));
    auto badFrameRate = fixture;
    putLe32(badFrameRate, 4, 192000);
    CHECK(!parseIqFrame(badFrameRate, 0));
    auto nonfinite = fixture;
    putLe32(nonfinite, kTciAudioHeaderBytes, 0x7f800000U);
    CHECK(!parseIqFrame(nonfinite, 0));

    BoundedIqQueue queue(3);
    queue.push(*decoded); // 2 samples
    queue.push(*decoded); // overflow drops the oldest sample
    std::array<std::complex<float>, 4> tooLargeRead;
    tooLargeRead.fill({-9.0f, -9.0f});
    CHECK(queue.pop(tooLargeRead) == 0);
    CHECK(queue.size() == 3);
    CHECK(tooLargeRead[0] == std::complex<float>(-9.0f, -9.0f));
    CHECK(tooLargeRead[3] == std::complex<float>(-9.0f, -9.0f));
    std::array<std::complex<float>, 4> output{};
    const auto copied = queue.pop(std::span(output).first(3));
    CHECK(copied == 3 && queue.size() == 0);
    CHECK(output[0] == decoded->samples[1]);
    CHECK(output[1] == decoded->samples[0]);
    CHECK(output[2] == decoded->samples[1]);
    queue.clear();

    std::cout << "synthetic TCI protocol fixtures passed\n";
    return 0;
}
