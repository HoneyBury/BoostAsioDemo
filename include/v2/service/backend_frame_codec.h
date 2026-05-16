#pragma once

#include "v2/service/backend_envelope.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace v2::service {

inline constexpr std::size_t kFrameLengthHeaderSize = 4;

[[nodiscard]] std::string encode_frame(const BackendEnvelope& envelope);

[[nodiscard]] std::optional<BackendEnvelope> decode_frame(std::string_view json_bytes);

[[nodiscard]] std::optional<BackendEnvelope> read_frame(
    boost::asio::ip::tcp::socket& socket,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

// SSL stream reads still use Boost.Asio blocking reads; the timeout argument is
// kept for API compatibility with the plain TCP overload.
[[nodiscard]] std::optional<BackendEnvelope> read_frame(
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>& stream,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

bool write_frame(boost::asio::ip::tcp::socket& socket,
                 const BackendEnvelope& envelope);

// v3.0.0: Write a frame through an SSL stream.
bool write_frame(boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>& stream,
                 const BackendEnvelope& envelope);

}  // namespace v2::service
