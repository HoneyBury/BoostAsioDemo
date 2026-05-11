#include "v2/service/backend_server.h"

#include "v2/service/backend_frame_codec.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>

namespace v2::service {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

BackendServer::BackendServer(std::uint16_t port, HandlerMap handlers)
    : port_(port), handlers_(std::move(handlers)) {}

BackendServer::~BackendServer() { stop(); }

void BackendServer::start() {
    running_ = true;

    acceptor_ = std::make_unique<tcp::acceptor>(
        io_context_, tcp::endpoint(tcp::v4(), port_));

    thread_ = std::thread([this] {
        do_accept();
        io_context_.run();
    });
}

void BackendServer::stop() {
    running_ = false;
    if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    if (session_socket_) {
        boost::system::error_code ec;
        session_socket_->close(ec);
    }
    io_context_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::uint16_t BackendServer::local_port() const {
    if (!acceptor_ || !acceptor_->is_open()) return 0;
    return acceptor_->local_endpoint().port();
}

void BackendServer::do_accept() {
    if (!running_) return;

    auto socket = std::make_shared<tcp::socket>(io_context_);
    acceptor_->async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (!ec && running_) {
            handle_session(std::move(socket));
        }
        do_accept();
    });
}

void BackendServer::handle_session(std::shared_ptr<tcp::socket> socket) {
    session_socket_ = socket;
    while (running_ && socket->is_open()) {
        auto request = read_frame(*socket, std::chrono::milliseconds(7000));
        if (!request) break;

        BackendEnvelope response;
        response.correlation_id = request->correlation_id;
        response.source_service = request->target_service;
        response.target_service = request->source_service;
        response.kind = MessageKind::kResponse;

        auto it = handlers_.find(request->message_type);
        if (it != handlers_.end()) {
            try {
                response = it->second(*request);
                response.correlation_id = request->correlation_id;
                if (response.source_service == ServiceId::kGateway) {
                    response.source_service = request->target_service;
                }
                if (response.target_service == ServiceId::kGateway) {
                    response.target_service = request->source_service;
                }
            } catch (...) {
                response.kind = MessageKind::kError;
                response.error_code = -1005;
                response.payload.clear();
            }
        } else {
            response.kind = MessageKind::kError;
            response.error_code = -1006;
            response.payload.clear();
        }

        write_frame(*socket, response);
    }
    session_socket_.reset();
}

}  // namespace v2::service
