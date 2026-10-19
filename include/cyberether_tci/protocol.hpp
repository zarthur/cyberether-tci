#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cyberether_tci {

constexpr std::size_t kTciAudioHeaderBytes = 64;
constexpr std::size_t kMaxIqFrameBytes = 1024 * 1024;
constexpr std::size_t kMaxControlRecordBytes = 4096;

struct ControlUpdate {
    enum class Kind { Ready, PanCenterHz, SampleRateHz, IqStarted, IqStopped };
    Kind kind;
    std::uint32_t receiver = 0;
    std::uint32_t value = 0;
};

// Incremental parser for the small part of TCI control state this source uses.
// Unknown server state records are ignored; malformed recognized records fail.
class ControlParser {
  public:
    bool append(std::string_view bytes, std::vector<ControlUpdate>& updates);
    void reset() noexcept { partial_.clear(); failed_ = false; }
    bool failed() const noexcept { return failed_; }

  private:
    bool parseRecord(std::string_view record, std::vector<ControlUpdate>& updates);
    std::string partial_;
    bool failed_ = false;
};

struct IqFrame {
    std::uint32_t receiver = 0;
    std::uint32_t sampleRateHz = 0;
    std::vector<std::complex<float>> samples;
};

// AetherSDR emits the 16-word, 64-byte native-little-endian TciAudioHeader
// followed by interleaved little-endian float32 I,Q pairs for type=0.
std::optional<IqFrame> parseIqFrame(std::span<const std::byte> bytes,
                                    std::uint32_t expectedReceiver);

bool isFlex6400IqRate(std::uint32_t rateHz) noexcept;

// Fixed-capacity producer/consumer queue. Overflow discards the oldest IQ so
// processing stays close to live time. pop is all-or-nothing: callers never
// receive zero-filled shortages as if they were radio samples.
class BoundedIqQueue {
  public:
    explicit BoundedIqQueue(std::size_t capacity);
    void clear() noexcept;
    void push(const IqFrame& frame);
    std::size_t pop(std::span<std::complex<float>> output) noexcept;
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return data_.size(); }

  private:
    std::vector<std::complex<float>> data_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

}  // namespace cyberether_tci
