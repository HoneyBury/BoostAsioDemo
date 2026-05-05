#include "app/logging.h"

#include <boost/asio.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

class EchoClient : public std::enable_shared_from_this<EchoClient> {
public:
    EchoClient(asio::io_context& io_context,
               std::string host,
               std::string port,
               std::string message)
        : resolver_(io_context),
          socket_(io_context),
          host_(std::move(host)),
          port_(std::move(port)),
          // 追加换行符，便于服务端和客户端用按行协议完成回显。
          message_(std::move(message) + "\n") {}

    void start() {
        auto self = shared_from_this();  // 保持客户端对象存活，直到异步解析完成。

        // `async_resolve` 会把主机名和端口解析成可连接的 TCP endpoint 列表。
        resolver_.async_resolve(
            host_,
            port_,
            [self](const error_code& ec, const tcp::resolver::results_type& endpoints) {
                if (ec) {
                    LOG_ERROR("Resolve failed: {}", ec.message());
                    return;
                }

                self->do_connect(endpoints);
            });
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints) {
        auto self = shared_from_this();  // 保持客户端对象存活，直到异步连接完成。

        // `async_connect` 会依次尝试这些 endpoint，直到其中一个连接成功。
        asio::async_connect(
            socket_,
            endpoints,
            [self](const error_code& ec, const tcp::endpoint& endpoint) {
                if (ec) {
                    LOG_ERROR("Connect failed: {}", ec.message());
                    return;
                }

                LOG_INFO("Connected to {}:{}", endpoint.address().to_string(), endpoint.port());
                self->do_write();
            });
    }

    void do_write() {
        auto self = shared_from_this();  // 保持客户端对象存活，直到发送完成。
        LOG_INFO("Sending: {}", message_);

        // `async_write` 保证整条消息完整发出，而不是只发送一部分。
        asio::async_write(
            socket_,
            asio::buffer(message_),
            [self](const error_code& ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    LOG_ERROR("Write failed: {}", ec.message());
                    return;
                }

                self->do_read();
            });
    }

    void do_read() {
        auto self = shared_from_this();  // 保持客户端对象存活，直到收到回显结果。

        // `async_read_until` 会持续读取，直到读到换行符为止。
        asio::async_read_until(
            socket_,
            response_buffer_,
            '\n',
            [self](const error_code& ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    LOG_ERROR("Read failed: {}", ec.message());
                    return;
                }

                std::istream stream(&self->response_buffer_);
                std::string echoed_line;
                std::getline(stream, echoed_line);
                LOG_INFO("Received echo: {}", echoed_line);

                error_code ignored_ec;
                self->socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
                self->socket_.close(ignored_ec);
            });
    }

    tcp::resolver resolver_;
    tcp::socket socket_;
    boost::asio::streambuf response_buffer_;
    std::string host_;
    std::string port_;
    std::string message_;
};

int main(int argc, char* argv[]) {
    app::logging::init("echo_client");  // 在启动异步网络逻辑前初始化日志。

    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string port = argc > 2 ? argv[2] : "9000";
    const std::string message = argc > 3 ? argv[3] : "hello from client";

    asio::io_context io_context;  // 客户端事件循环，负责驱动所有异步操作。
    auto client = std::make_shared<EchoClient>(io_context, host, port, message);
    client->start();

    // `run()` 会阻塞主线程，直到客户端相关的异步操作全部执行完成。
    io_context.run();
    return 0;
}
