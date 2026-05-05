#include "app/logging.h"

#include <boost/asio.hpp>

#include <array>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

class EchoSession : public std::enable_shared_from_this<EchoSession> {
public:
    explicit EchoSession(tcp::socket socket)
        : socket_(std::move(socket)),
          // `strand_` 保证同一会话上的回调串行执行，即使有多个 `io_context.run()` 线程。
          strand_(asio::make_strand(socket_.get_executor())) {}

    void start() {
        // 启动会话后，立即进入异步读循环。
        do_read();
    }

private:
    void do_read() {
        auto self = shared_from_this();  // 保持会话对象存活，直到当前回调执行完成。

        // `async_read_some` 读取当前已经就绪的那部分数据。
        socket_.async_read_some(
            asio::buffer(read_buffer_),
            asio::bind_executor(strand_,
                                [self](const error_code& ec, std::size_t bytes_transferred) {
                                    if (ec) {
                                        LOG_INFO("Client {} disconnected: {}",
                                                 self->remote_endpoint(),
                                                 ec.message());
                                        return;
                                    }

                                    // 这里只拷贝本次实际读到的字节，避免下次读取覆盖固定缓冲区。
                                    std::string message(self->read_buffer_.data(), bytes_transferred);
                                    LOG_INFO("Received from {}: {}",
                                             self->remote_endpoint(),
                                             message);

                                    self->enqueue_write(std::move(message));
                                    self->do_read();  // 继续挂起下一次异步读，保持 echo 循环。
                                }));
    }

    void enqueue_write(std::string message) {
        auto self = shared_from_this();  // 保持会话对象存活，直到入队动作完成。

        // `post` 把发送队列操作投递回同一个执行器，避免跨线程直接修改会话状态。
        asio::post(strand_, [self, message = std::move(message)]() mutable {
            const bool write_in_progress = !self->write_queue_.empty();
            self->write_queue_.push_back(std::move(message));

            // 只有从空队列切换到非空队列时，才真正启动新的异步写链路。
            if (!write_in_progress) {
                self->do_write();
            }
        });
    }

    void do_write() {
        auto self = shared_from_this();  // 保持会话对象存活，直到当前写操作完成。

        // `async_write` 会持续发送，直到队首消息完整写出。
        asio::async_write(
            socket_,
            asio::buffer(write_queue_.front()),
            asio::bind_executor(strand_,
                                [self](const error_code& ec, std::size_t bytes_transferred) {
                                    if (ec) {
                                        LOG_WARN("Write to {} failed: {}",
                                                 self->remote_endpoint(),
                                                 ec.message());
                                        return;
                                    }

                                    LOG_DEBUG("Echoed {} bytes to {}",
                                              bytes_transferred,
                                              self->remote_endpoint());

                                    self->write_queue_.pop_front();

                                    // 队列里还有数据就继续发，保证按入队顺序回写。
                                    if (!self->write_queue_.empty()) {
                                        self->do_write();
                                    }
                                }));
    }

    std::string remote_endpoint() const {
        error_code ec;
        const auto endpoint = socket_.remote_endpoint(ec);
        if (ec) {
            return "<unknown>";
        }

        return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    }

    tcp::socket socket_;
    asio::strand<asio::any_io_executor> strand_;
    std::array<char, 1024> read_buffer_{};
    std::deque<std::string> write_queue_;
};

class EchoServer {
public:
    EchoServer(asio::io_context& io_context, std::uint16_t port)
        : io_context_(io_context),
          // `acceptor_` 持有监听 socket，负责接收新的 TCP 连接。
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {}

    void start() {
        LOG_INFO("Echo server listening on 0.0.0.0:{}", acceptor_.local_endpoint().port());
        do_accept();
    }

private:
    void do_accept() {
        // `async_accept` 异步等待新连接，不会阻塞线程。
        acceptor_.async_accept([this](const error_code& ec, tcp::socket socket) {
            if (!ec) {
                LOG_INFO("Accepted client {}", socket.remote_endpoint().address().to_string());
                std::make_shared<EchoSession>(std::move(socket))->start();
            } else {
                LOG_ERROR("Accept failed: {}", ec.message());
            }

            // 立刻挂起下一次 accept，确保服务端可以持续处理多个客户端。
            do_accept();
        });
    }

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    app::logging::init("echo_server");  // 在启动网络逻辑前初始化全局日志。

    const auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 9000);

    asio::io_context io_context;  // 事件循环核心对象，负责驱动异步操作完成回调。
    EchoServer server(io_context, port);
    server.start();

    // 启动多个事件循环线程，让多个客户端连接可以并发推进。
    const auto thread_count =
        std::max(2u, std::thread::hardware_concurrency() == 0 ? 2u : std::thread::hardware_concurrency());

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (unsigned int i = 0; i < thread_count; ++i) {
        workers.emplace_back([&io_context]() {
            // `run()` 会阻塞在这里，把已完成的异步操作分发到当前线程执行。
            io_context.run();
        });
    }

    for (auto& worker : workers) {
        worker.join();  // 等待所有工作线程，保证服务进程持续运行。
    }

    return 0;
}
