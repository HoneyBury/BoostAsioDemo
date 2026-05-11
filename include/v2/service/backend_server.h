#pragma once

#include "v2/service/backend_envelope.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace v2::service {

using BackendHandler = std::function<BackendEnvelope(const BackendEnvelope& request)>;

class BackendServer {
public:
    using HandlerMap = std::unordered_map<std::string, BackendHandler>;

    BackendServer(std::uint16_t port, HandlerMap handlers);
    ~BackendServer();

    BackendServer(const BackendServer&) = delete;
    BackendServer& operator=(const BackendServer&) = delete;
    BackendServer(BackendServer&&) = delete;
    BackendServer& operator=(BackendServer&&) = delete;

    void start();
    void stop();

    [[nodiscard]] std::uint16_t local_port() const;

private:
    void do_accept();
    void handle_session(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    std::uint16_t port_;
    HandlerMap handlers_;
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::shared_ptr<boost::asio::ip::tcp::socket> session_socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace v2::service
