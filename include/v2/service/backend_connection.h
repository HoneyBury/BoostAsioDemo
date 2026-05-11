#pragma once

#include "v2/service/backend_envelope.h"

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace v2::service {

struct BackendConnectionOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000);
    std::chrono::milliseconds connect_timeout = std::chrono::milliseconds(1000);
};

class BackendConnection {
public:
    explicit BackendConnection(BackendConnectionOptions options);
    ~BackendConnection();

    BackendConnection(const BackendConnection&) = delete;
    BackendConnection& operator=(const BackendConnection&) = delete;
    BackendConnection(BackendConnection&&) = delete;
    BackendConnection& operator=(BackendConnection&&) = delete;

    bool connect();

    [[nodiscard]] std::optional<BackendEnvelope> send_request(
        BackendEnvelope request);

    void close();

    [[nodiscard]] bool is_connected() const;

private:
    BackendConnectionOptions options_;
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
};

}  // namespace v2::service
