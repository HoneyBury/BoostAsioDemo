#include "v2/service/backend_frame_codec.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace v2::service {

namespace asio = boost::asio;

namespace {

std::array<std::byte, kFrameLengthHeaderSize> encode_length(std::uint32_t length) {
    std::array<std::byte, kFrameLengthHeaderSize> header{};
    header[0] = static_cast<std::byte>(length & 0xFF);
    header[1] = static_cast<std::byte>((length >> 8) & 0xFF);
    header[2] = static_cast<std::byte>((length >> 16) & 0xFF);
    header[3] = static_cast<std::byte>((length >> 24) & 0xFF);
    return header;
}

std::uint32_t decode_length(const std::array<std::byte, kFrameLengthHeaderSize>& header) {
    return (static_cast<std::uint32_t>(header[0])) |
           (static_cast<std::uint32_t>(header[1]) << 8) |
           (static_cast<std::uint32_t>(header[2]) << 16) |
           (static_cast<std::uint32_t>(header[3]) << 24);
}

bool read_exactly_with_timeout(asio::ip::tcp::socket& socket,
                               void* data,
                               std::size_t length,
                               std::chrono::milliseconds timeout) {
    boost::system::error_code ec;
    socket.non_blocking(true, ec);
    if (ec) {
        return false;
    }

    std::size_t total = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto* out = static_cast<char*>(data);
    while (total < length) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline || !socket.is_open()) {
            socket.non_blocking(false, ec);
            return false;
        }

        const auto remaining = length - total;
        const auto read = socket.read_some(asio::buffer(out + total, remaining), ec);
        if (!ec) {
            total += read;
            continue;
        }
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        socket.non_blocking(false, ec);
        return false;
    }

    socket.non_blocking(false, ec);
    return true;
}

}  // namespace

std::string encode_frame(const BackendEnvelope& envelope) {
    const auto json = to_json(envelope);
    const auto length = static_cast<std::uint32_t>(json.size());
    const auto header = encode_length(length);

    std::string frame;
    frame.reserve(kFrameLengthHeaderSize + json.size());
    frame.append(reinterpret_cast<const char*>(header.data()), kFrameLengthHeaderSize);
    frame.append(json);
    return frame;
}

std::optional<BackendEnvelope> decode_frame(std::string_view json_bytes) {
    return from_json(json_bytes);
}

std::optional<BackendEnvelope> read_frame(asio::ip::tcp::socket& socket,
                                          std::chrono::milliseconds timeout) {
    std::array<std::byte, kFrameLengthHeaderSize> header{};
    if (!read_exactly_with_timeout(socket, header.data(), kFrameLengthHeaderSize, timeout)) {
        return std::nullopt;
    }

    const auto payload_length = decode_length(header);
    if (payload_length == 0 || payload_length > 1024 * 1024) return std::nullopt;

    std::string json_bytes(payload_length, '\0');
    if (!read_exactly_with_timeout(socket, json_bytes.data(), payload_length, timeout)) {
        return std::nullopt;
    }

    return from_json(json_bytes);
}

bool write_frame(asio::ip::tcp::socket& socket, const BackendEnvelope& envelope) {
    const auto frame = encode_frame(envelope);
    boost::system::error_code ec;
    asio::write(socket, asio::buffer(frame.data(), frame.size()),
                asio::transfer_exactly(frame.size()), ec);
    return !ec;
}

// ── SSL stream overloads ──────────────────────────────────────────────

std::optional<BackendEnvelope> read_frame(
    asio::ssl::stream<asio::ip::tcp::socket&>& stream,
    std::chrono::milliseconds timeout) {
    (void)timeout;
    boost::system::error_code ec;
    std::array<std::byte, kFrameLengthHeaderSize> header{};
    asio::read(stream, asio::buffer(header.data(), kFrameLengthHeaderSize),
               asio::transfer_exactly(kFrameLengthHeaderSize), ec);
    if (ec) return std::nullopt;

    const auto payload_length = decode_length(header);
    if (payload_length == 0 || payload_length > 1024 * 1024) return std::nullopt;

    std::string json_bytes(payload_length, '\0');
    asio::read(stream, asio::buffer(json_bytes.data(), payload_length),
               asio::transfer_exactly(payload_length), ec);
    if (ec) return std::nullopt;

    return from_json(json_bytes);
}

bool write_frame(asio::ssl::stream<asio::ip::tcp::socket&>& stream,
                 const BackendEnvelope& envelope) {
    const auto frame = encode_frame(envelope);
    boost::system::error_code ec;
    asio::write(stream, asio::buffer(frame.data(), frame.size()),
                asio::transfer_exactly(frame.size()), ec);
    return !ec;
}

}  // namespace v2::service
