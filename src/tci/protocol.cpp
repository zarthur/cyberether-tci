#include "cyberether_tci/protocol.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

namespace cyberether_tci {
namespace {

bool parseUnsigned(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    std::uint64_t wide = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), wide);
    if (error != std::errc{} || end != text.data() + text.size() ||
        wide > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    value = static_cast<std::uint32_t>(wide);
    return true;
}

std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24);
}

}  // namespace

bool isFlex6400IqRate(std::uint32_t rateHz) noexcept {
    return rateHz == 24000 || rateHz == 48000 || rateHz == 96000;
}

bool ControlParser::append(std::string_view bytes, std::vector<ControlUpdate>& updates) {
    if (failed_) return false;
    for (const char byte : bytes) {
        if (byte == ';') {
            if (!parseRecord(partial_, updates)) {
                failed_ = true;
                partial_.clear();
                return false;
            }
            partial_.clear();
        } else {
            if (partial_.size() == kMaxControlRecordBytes) {
                failed_ = true;
                partial_.clear();
                return false;
            }
            partial_.push_back(byte);
        }
    }
    return true;
}

bool ControlParser::parseRecord(std::string_view record, std::vector<ControlUpdate>& updates) {
    if (record == "ready") {
        updates.push_back({ControlUpdate::Kind::Ready});
        return true;
    }

    const auto colon = record.find(':');
    if (colon == std::string_view::npos) return true;  // irrelevant server state
    const auto name = record.substr(0, colon);
    const auto args = record.substr(colon + 1);

    if (name == "iq_samplerate") {
        std::uint32_t rate = 0;
        if (!parseUnsigned(args, rate) || !isFlex6400IqRate(rate)) return false;
        updates.push_back({ControlUpdate::Kind::SampleRateHz, 0, rate});
        return true;
    }

    if (name == "dds") {
        const auto comma = args.find(',');
        if (comma == std::string_view::npos || args.find(',', comma + 1) != std::string_view::npos) {
            return false;
        }
        std::uint32_t receiver = 0;
        std::uint32_t centerHz = 0;
        if (!parseUnsigned(args.substr(0, comma), receiver) || receiver > 3 ||
            !parseUnsigned(args.substr(comma + 1), centerHz) || centerHz == 0) {
            return false;
        }
        updates.push_back({ControlUpdate::Kind::PanCenterHz, receiver, centerHz});
        return true;
    }

    if (name == "iq_start" || name == "iq_stop") {
        std::uint32_t receiver = 0;
        if (!parseUnsigned(args, receiver) || receiver > 3) return false;
        updates.push_back({name == "iq_start" ? ControlUpdate::Kind::IqStarted
                                               : ControlUpdate::Kind::IqStopped,
                           receiver, 0});
    }
    return true;
}

std::optional<IqFrame> parseIqFrame(std::span<const std::byte> bytes,
                                    std::uint32_t expectedReceiver) {
    if (bytes.size() < kTciAudioHeaderBytes || bytes.size() > kMaxIqFrameBytes ||
        expectedReceiver > 3) {
        return std::nullopt;
    }

    const auto receiver = readLe32(bytes, 0);
    const auto sampleRate = readLe32(bytes, 4);
    const auto format = readLe32(bytes, 8);
    const auto codec = readLe32(bytes, 12);
    const auto crc = readLe32(bytes, 16);
    const auto scalarCount = readLe32(bytes, 20);
    const auto type = readLe32(bytes, 24);
    const auto channels = readLe32(bytes, 28);

    if (receiver != expectedReceiver || receiver > 3 || !isFlex6400IqRate(sampleRate) ||
        format != 3 || codec != 0 || crc != 0 || type != 0 || channels != 2 ||
        scalarCount == 0 || (scalarCount & 1U) != 0) {
        return std::nullopt;
    }
    for (std::size_t word = 8; word < 16; ++word) {
        if (readLe32(bytes, word * 4) != 0) return std::nullopt;
    }

    constexpr auto maxScalars = (kMaxIqFrameBytes - kTciAudioHeaderBytes) / sizeof(float);
    if (scalarCount > maxScalars) return std::nullopt;
    const std::size_t payloadBytes = static_cast<std::size_t>(scalarCount) * sizeof(float);
    if (bytes.size() != kTciAudioHeaderBytes + payloadBytes) {
        return std::nullopt;
    }

    IqFrame frame;
    frame.receiver = receiver;
    frame.sampleRateHz = sampleRate;
    frame.samples.reserve(scalarCount / 2);
    for (std::size_t i = 0; i < scalarCount; i += 2) {
        const auto iBits = readLe32(bytes, kTciAudioHeaderBytes + i * sizeof(float));
        const auto qBits = readLe32(bytes, kTciAudioHeaderBytes + (i + 1) * sizeof(float));
        const float inPhase = std::bit_cast<float>(iBits);
        const float quadrature = std::bit_cast<float>(qBits);
        if (!std::isfinite(inPhase) || !std::isfinite(quadrature)) return std::nullopt;
        frame.samples.emplace_back(inPhase, quadrature);
    }
    return frame;
}

BoundedIqQueue::BoundedIqQueue(std::size_t capacity) : data_(capacity) {}

void BoundedIqQueue::clear() noexcept {
    head_ = 0;
    size_ = 0;
}

void BoundedIqQueue::push(const IqFrame& frame) {
    if (data_.empty() || frame.samples.empty()) return;
    std::size_t first = 0;
    std::size_t count = frame.samples.size();
    if (count > data_.size()) {
        first = count - data_.size();
        count = data_.size();
    }
    for (std::size_t i = first; i < frame.samples.size(); ++i) {
        if (size_ == data_.size()) {
            head_ = (head_ + 1) % data_.size();
            --size_;
        }
        data_[(head_ + size_) % data_.size()] = frame.samples[i];
        ++size_;
    }
}

std::size_t BoundedIqQueue::pop(std::span<std::complex<float>> output) noexcept {
    if (output.size() > size_) return 0;
    const std::size_t count = output.size();
    for (std::size_t i = 0; i < count; ++i) {
        output[i] = data_[(head_ + i) % data_.size()];
    }
    if (!data_.empty()) head_ = (head_ + count) % data_.size();
    size_ -= count;
    return count;
}

}  // namespace cyberether_tci
