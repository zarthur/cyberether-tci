#include "cyberether_tci/client.hpp"
#include "connect_wait.hpp"
#include "socket_io.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <random>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cyberether_tci {
namespace {

constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxHttpResponse = 16384;
constexpr std::size_t kMaxWebSocketMessage = kMaxIqFrameBytes + kTciAudioHeaderBytes;
constexpr auto kConnectTimeout = std::chrono::seconds(3);

enum class WsOpcode : std::uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

std::uint32_t rotateLeft(std::uint32_t value, unsigned count) {
    return (value << count) | (value >> (32 - count));
}

std::array<std::byte, 20> sha1(std::string_view input) {
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>(bitLength >> shift));

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xefcdab89U;
    std::uint32_t h2 = 0x98badcfeU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xc3d2e1f0U;
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto at = offset + i * 4;
            words[i] = (std::uint32_t(data[at]) << 24) | (std::uint32_t(data[at + 1]) << 16) |
                       (std::uint32_t(data[at + 2]) << 8) | std::uint32_t(data[at + 3]);
        }
        for (std::size_t i = 16; i < 80; ++i) words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5a827999U; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1U; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcU; }
            else { f = b ^ c ^ d; k = 0xca62c1d6U; }
            const auto temp = rotateLeft(a, 5) + f + e + k + words[i];
            e = d; d = c; c = rotateLeft(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::array<std::byte, 20> digest{};
    const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < words.size(); ++i) {
        for (unsigned byte = 0; byte < 4; ++byte) digest[i * 4 + byte] = std::byte((words[i] >> (24 - byte * 8)) & 0xffU);
    }
    return digest;
}

std::string base64(std::span<const std::byte> data) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const auto a = std::to_integer<unsigned>(data[i]);
        const auto b = i + 1 < data.size() ? std::to_integer<unsigned>(data[i + 1]) : 0U;
        const auto c = i + 2 < data.size() ? std::to_integer<unsigned>(data[i + 2]) : 0U;
        const auto value = (a << 16) | (b << 8) | c;
        out.push_back(alphabet[(value >> 18) & 63]);
        out.push_back(alphabet[(value >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? alphabet[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? alphabet[value & 63] : '=');
    }
    return out;
}

bool sendAll(int fd, std::span<const std::byte> bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto rc = detail::sendWithoutSigPipe(fd, bytes.data() + sent, bytes.size() - sent);
        if (rc <= 0) return false;
        sent += static_cast<std::size_t>(rc);
    }
    return true;
}

bool recvExact(int fd, std::span<std::byte> bytes) {
    std::size_t received = 0;
    while (received < bytes.size()) {
        const auto rc = ::recv(fd, bytes.data() + received, bytes.size() - received, 0);
        if (rc <= 0) return false;
        received += static_cast<std::size_t>(rc);
    }
    return true;
}

int connectTcp(const ClientConfig& config, const std::atomic<bool>& stopping) {
    // Initial support deliberately avoids DNS: AetherSDR runs on this Mac and
    // the TCI listener is unauthenticated, so only an IPv4 loopback literal is
    // accepted. This also bounds cancellation during endpoint resolution.
    if (config.host != "127.0.0.1" || config.port == 0 || stopping.load()) {
        return -1;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (!detail::configureSocketForSend(fd)) {
        ::close(fd);
        return -1;
    }

    const int originalFlags = ::fcntl(fd, F_GETFL, 0);
    if (originalFlags < 0 || ::fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) != 0) {
        ::close(fd);
        return -1;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(config.port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectResult = ::connect(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
    if (connectResult != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }

    if (connectResult != 0) {
        const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
        pollfd descriptor{fd, POLLOUT, 0};
        const auto waitResult = detail::waitForWritableUntil<std::chrono::steady_clock>(
            stopping, deadline, [&descriptor](std::chrono::milliseconds timeout) {
                return ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
            });
        if (waitResult != detail::ConnectWaitResult::Writable) {
            ::close(fd);
            return -1;
        }
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0 || socketError != 0) {
            ::close(fd);
            return -1;
        }
    }
    if (stopping.load()) {
        ::close(fd);
        return -1;
    }
    if (::fcntl(fd, F_SETFL, originalFlags) != 0) {
        ::close(fd);
        return -1;
    }
    timeval timeout{3, 0};
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    return fd;
}

bool websocketHandshake(int fd, const ClientConfig& config) {
    std::array<std::byte, 16> nonce{};
    std::random_device random;
    for (auto& byte : nonce) byte = std::byte(random() & 0xffU);
    const auto key = base64(nonce);
    const auto hostHeader = config.host.find(':') == std::string::npos
                                ? config.host + ":" + std::to_string(config.port)
                                : "[" + config.host + "]:" + std::to_string(config.port);
    const std::string request = "GET / HTTP/1.1\r\nHost: " + hostHeader +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
        "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!sendAll(fd, std::as_bytes(std::span(request.data(), request.size())))) return false;

    std::string response;
    response.reserve(1024);
    std::array<std::byte, 1> byte{};
    while (response.size() < kMaxHttpResponse && !response.ends_with("\r\n\r\n")) {
        if (!recvExact(fd, byte)) return false;
        response.push_back(static_cast<char>(std::to_integer<unsigned>(byte[0])));
    }
    if (!response.ends_with("\r\n\r\n") || response.find(" 101 ") == std::string::npos) return false;

    std::string expected = key + std::string(kWebSocketGuid);
    const auto digest = sha1(expected);
    expected = base64(digest);
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto field = lower.find("sec-websocket-accept:");
    if (field == std::string::npos) return false;
    auto begin = response.find(':', field) + 1;
    while (begin < response.size() && (response[begin] == ' ' || response[begin] == '\t')) ++begin;
    const auto end = response.find("\r\n", begin);
    return end != std::string::npos && response.substr(begin, end - begin) == expected;
}

bool sendMaskedFrame(int fd, WsOpcode opcode, std::span<const std::byte> payload) {
    if (payload.size() > kMaxWebSocketMessage) return false;
    std::vector<std::byte> frame;
    frame.reserve(payload.size() + 14);
    frame.push_back(std::byte(0x80U | static_cast<std::uint8_t>(opcode)));
    if (payload.size() < 126) {
        frame.push_back(std::byte(0x80U | static_cast<std::uint8_t>(payload.size())));
    } else if (payload.size() <= 0xffffU) {
        frame.push_back(std::byte(0x80U | 126U));
        frame.push_back(std::byte((payload.size() >> 8) & 0xffU));
        frame.push_back(std::byte(payload.size() & 0xffU));
    } else {
        frame.push_back(std::byte(0x80U | 127U));
        const auto length = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(std::byte((length >> shift) & 0xffU));
    }
    std::array<std::byte, 4> mask{};
    std::random_device random;
    for (auto& byte : mask) byte = std::byte(random() & 0xffU);
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (std::size_t i = 0; i < payload.size(); ++i) frame.push_back(payload[i] ^ mask[i % mask.size()]);
    return sendAll(fd, frame);
}

struct WsMessage { WsOpcode opcode; std::vector<std::byte> payload; };

bool readWebSocketMessage(int fd, WsMessage& message) {
    std::array<std::byte, 2> header{};
    if (!recvExact(fd, header)) return false;
    const auto first = std::to_integer<unsigned>(header[0]);
    const auto second = std::to_integer<unsigned>(header[1]);
    if ((first & 0x70U) != 0 || (first & 0x80U) == 0 || (second & 0x80U) != 0) return false;
    const auto opcode = first & 0x0fU;
    if (opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10) return false;
    std::uint64_t length = second & 0x7fU;
    if (length == 126) {
        std::array<std::byte, 2> extended{};
        if (!recvExact(fd, extended)) return false;
        length = (std::to_integer<std::uint64_t>(extended[0]) << 8) | std::to_integer<std::uint64_t>(extended[1]);
    } else if (length == 127) {
        std::array<std::byte, 8> extended{};
        if (!recvExact(fd, extended)) return false;
        length = 0;
        for (auto byte : extended) length = (length << 8) | std::to_integer<std::uint64_t>(byte);
        if ((length >> 63) != 0) return false;
    }
    if (length > kMaxWebSocketMessage || (opcode >= 8 && length > 125)) return false;
    message.opcode = static_cast<WsOpcode>(opcode);
    message.payload.resize(static_cast<std::size_t>(length));
    return message.payload.empty() || recvExact(fd, message.payload);
}

}  // namespace

TciClient::~TciClient() { stop(); }

bool TciClient::start(ClientConfig config) {
    if (worker_.joinable() || config.host != "127.0.0.1" || config.receiver > 3 ||
        !isFlex6400IqRate(config.requestedRateHz) || config.queueCapacity == 0) return false;
    config_ = std::move(config);
    queue_ = BoundedIqQueue(config_.queueCapacity);
    stopping_.store(false);
    worker_ = std::thread([this] { run(); });
    return true;
}

void TciClient::stop() noexcept {
    stopping_.store(true);
    stateChanged_.notify_all();
    {
        std::lock_guard sendLock(sendMutex_);
        const auto fd = socketFd_.load();
        if (fd >= 0) {
            bool connected = false;
            {
                std::lock_guard stateLock(stateMutex_);
                connected = metadata_.connected;
            }
            if (connected) {
                const auto command = "iq_stop:" + std::to_string(config_.receiver) + ";";
                (void)sendMaskedFrame(fd, WsOpcode::Text, std::as_bytes(std::span(command.data(), command.size())));
            }
            (void)::shutdown(fd, SHUT_RDWR);
        }
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    clearSession();
}

bool TciClient::waitForSamples(std::size_t required, std::chrono::milliseconds timeout) const noexcept {
    std::unique_lock lock(stateMutex_);
    const bool ready = stateChanged_.wait_for(lock, timeout, [this, required] {
        return stopping_.load() ||
               (metadata_.connected && metadata_.ready && metadata_.iqStarted && queue_.size() >= required);
    });
    return ready && !stopping_.load() && metadata_.connected && metadata_.ready &&
           metadata_.iqStarted && queue_.size() >= required;
}

StreamReadResult TciClient::popWithMetadata(std::span<std::complex<float>> output) noexcept {
    std::lock_guard lock(stateMutex_);
    StreamReadResult result{metadata_, 0};
    if (!metadata_.connected || !metadata_.ready || !metadata_.iqStarted || queue_.size() < output.size()) {
        return result;
    }
    result.samplesCopied = queue_.pop(output);
    return result;
}

StreamMetadata TciClient::metadata() const noexcept {
    std::lock_guard lock(stateMutex_);
    return metadata_;
}

void TciClient::clearSession() noexcept {
    {
        std::lock_guard lock(stateMutex_);
        metadata_.sampleRateHz = 0;
        metadata_.panCenterHz = 0;
        metadata_.ready = false;
        metadata_.iqStarted = false;
        metadata_.connected = false;
        iqStartRequested_ = false;
        ++metadata_.epoch;
        if (metadata_.epoch == 0) ++metadata_.epoch;
        queue_.clear();
    }
    stateChanged_.notify_all();
}

void TciClient::run() {
    auto backoff = std::chrono::milliseconds(250);
    while (!stopping_.load()) {
        if (runConnection()) backoff = std::chrono::milliseconds(250);
        clearSession();
        if (stopping_.load()) break;
        std::unique_lock lock(waitMutex_);
        wake_.wait_for(lock, backoff, [this] { return stopping_.load(); });
        backoff = std::min(backoff * 2, std::chrono::milliseconds(5000));
    }
}

bool TciClient::runConnection() {
    const auto fd = connectTcp(config_, stopping_);
    if (fd < 0) return false;
    socketFd_.store(fd);
    const auto closeSocket = [this, fd] {
        std::lock_guard sendLock(sendMutex_);
        socketFd_.store(-1);
        ::close(fd);
    };

    bool success = false;
    do {
        if (stopping_.load() || !websocketHandshake(fd, config_)) break;
        {
            std::lock_guard stateLock(stateMutex_);
            metadata_.connected = true;
        }
        stateChanged_.notify_all();
        const auto rate = "iq_samplerate:" + std::to_string(config_.requestedRateHz) + ";";
        const auto start = "iq_start:" + std::to_string(config_.receiver) + ";";
        bool sent = false;
        {
            std::lock_guard sendLock(sendMutex_);
            sent = sendMaskedFrame(fd, WsOpcode::Text, std::as_bytes(std::span(rate.data(), rate.size()))) &&
                   sendMaskedFrame(fd, WsOpcode::Text, std::as_bytes(std::span(start.data(), start.size())));
        }
        if (!sent) break;
        {
            std::lock_guard stateLock(stateMutex_);
            // readWebSocketMessage runs only after this point, so no reply can
            // be mistaken for a subscription requested by an earlier session.
            iqStartRequested_ = true;
        }

        ControlParser parser;
        while (!stopping_.load()) {
            WsMessage message;
            if (!readWebSocketMessage(fd, message)) break;
            if (message.opcode == WsOpcode::Ping) {
                std::lock_guard sendLock(sendMutex_);
                if (!sendMaskedFrame(fd, WsOpcode::Pong, message.payload)) break;
                continue;
            }
            if (message.opcode == WsOpcode::Close) break;
            if (message.opcode == WsOpcode::Pong) continue;
            if (message.opcode == WsOpcode::Text) {
                if (message.payload.empty()) continue;
                const std::string_view text(reinterpret_cast<const char*>(message.payload.data()), message.payload.size());
                std::vector<ControlUpdate> updates;
                if (!parser.append(text, updates)) break;
                bool changed = false;
                {
                    std::lock_guard stateLock(stateMutex_);
                    const auto previousRate = metadata_.sampleRateHz;
                    const auto previousCenter = metadata_.panCenterHz;
                    for (const auto& update : updates) {
                        switch (update.kind) {
                            case ControlUpdate::Kind::Ready: metadata_.ready = true; break;
                            case ControlUpdate::Kind::PanCenterHz:
                                if (update.receiver == config_.receiver) metadata_.panCenterHz = update.value;
                                break;
                            case ControlUpdate::Kind::SampleRateHz: metadata_.sampleRateHz = update.value; break;
                            case ControlUpdate::Kind::IqStarted:
                                if (update.receiver == config_.receiver && iqStartRequested_) {
                                    metadata_.iqStarted = true;
                                }
                                break;
                            case ControlUpdate::Kind::IqStopped:
                                if (update.receiver == config_.receiver) {
                                    // A stop response/refusal closes this
                                    // subscription. Delayed binary frames must
                                    // not re-arm it without a new start request.
                                    iqStartRequested_ = false;
                                    metadata_.iqStarted = false;
                                    queue_.clear();
                                }
                                break;
                        }
                    }
                    changed = previousRate != metadata_.sampleRateHz || previousCenter != metadata_.panCenterHz;
                    if (changed) {
                        ++metadata_.epoch;
                        if (metadata_.epoch == 0) ++metadata_.epoch;
                        queue_.clear();
                    }
                }
                stateChanged_.notify_all();
            } else if (message.opcode == WsOpcode::Binary) {
                const auto frame = parseIqFrame(message.payload, config_.receiver);
                if (!frame) continue;
                {
                    std::lock_guard stateLock(stateMutex_);
                    if (!metadata_.connected || !iqStartRequested_ || !metadata_.ready ||
                        metadata_.sampleRateHz == 0 || metadata_.panCenterHz == 0 ||
                        frame->sampleRateHz != metadata_.sampleRateHz) continue;
                    // iq_start acknowledgements are optional in AetherSDR
                    // 26.9.1. A valid, receiver-matched frame in the current
                    // confirmed metadata epoch is the fallback confirmation.
                    metadata_.iqStarted = true;
                    queue_.push(*frame);
                }
                stateChanged_.notify_all();
            }
        }
        success = true;
    } while (false);

    closeSocket();
    clearSession();
    return success;
}

}  // namespace cyberether_tci
