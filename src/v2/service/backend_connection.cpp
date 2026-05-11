#include "v2/service/backend_connection.h"

#include "v2/service/backend_frame_codec.h"

namespace v2::service {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

BackendConnection::BackendConnection(BackendConnectionOptions options)
    : options_(std::move(options)) {}

BackendConnection::~BackendConnection() { close(); }

bool BackendConnection::connect() {
    socket_ = std::make_unique<tcp::socket>(io_context_);

    tcp::resolver resolver(io_context_);
    boost::system::error_code ec;
    auto endpoints = resolver.resolve(options_.host,
                                      std::to_string(options_.port), ec);
    if (ec) return false;

    asio::connect(*socket_, endpoints, ec);
    return !ec;
}

std::optional<BackendEnvelope> BackendConnection::send_request(
    BackendEnvelope request) {
    if (!socket_ || !socket_->is_open()) return std::nullopt;

    if (request.correlation_id == 0) {
        request.correlation_id = generate_correlation_id();
    }
    request.source_service = ServiceId::kGateway;

    if (!write_frame(*socket_, request)) return std::nullopt;

    auto response = read_frame(*socket_, options_.timeout);
    if (!response) return std::nullopt;

    return response;
}

void BackendConnection::close() {
    if (socket_) {
        boost::system::error_code ec;
        socket_->close(ec);
    }
    io_context_.stop();
}

bool BackendConnection::is_connected() const {
    return socket_ && socket_->is_open();
}

}  // namespace v2::service
