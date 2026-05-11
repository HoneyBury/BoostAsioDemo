#include "app/logging.h"
#include "net/packet_codec.h"
#include "net/protocol.h"
#include "v2/service/backend_connection.h"
#include "v2/service/backend_envelope.h"
#include "v2/service/backend_server.h"

#include <boost/asio.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

constexpr const char* kGatewayHost = "127.0.0.1";

// ─── Backend helpers ──────────────────────────────────────────────

v2::service::BackendServer::HandlerMap make_login_handlers() {
    v2::service::BackendServer::HandlerMap handlers;

    handlers["login_request"] = [](const v2::service::BackendEnvelope& request) {
        v2::service::BackendEnvelope response;
        response.kind = v2::service::MessageKind::kResponse;

        auto& payload = request.payload;
        if (payload.empty()) {
            response.kind = v2::service::MessageKind::kError;
            response.error_code = -1004;
            return response;
        }

        auto first_pipe = payload.find('|');
        if (first_pipe == std::string::npos) {
            response.kind = v2::service::MessageKind::kError;
            response.error_code = -1004;
            return response;
        }

        std::string user_id = payload.substr(0, first_pipe);
        auto token_start = payload.find("token:");
        if (token_start == std::string::npos || token_start + 6 >= payload.size()) {
            response.kind = v2::service::MessageKind::kError;
            response.error_code = -1004;
            response.payload = "empty_token";
            return response;
        }

        response.payload = "login_ok:" + user_id;
        return response;
    };

    // Reject empty tokens for error testing
    handlers["reject_empty"] = [](const v2::service::BackendEnvelope& /*request*/) {
        v2::service::BackendEnvelope response;
        response.kind = v2::service::MessageKind::kError;
        response.error_code = -1004;
        response.payload = "rejected";
        return response;
    };

    return handlers;
}

// ─── Backend server wrapper ───────────────────────────────────────

struct BackendProcess {
    std::unique_ptr<v2::service::BackendServer> server;
    std::uint16_t port = 0;

    bool start() {
        server = std::make_unique<v2::service::BackendServer>(0, make_login_handlers());
        server->start();
        port = server->local_port();
        return port > 0;
    }

    void stop() {
        if (server) server->stop();
    }
};

// ─── Minimal gateway that bridges client packets to backend ───────

struct BridgeGateway {
    asio::io_context io_context;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::unique_ptr<v2::service::BackendConnection> backend_conn;
    std::thread thread;
    std::atomic<bool> running{false};

    bool start(std::uint16_t own_port, std::uint16_t backend_port) {
        try {
            v2::service::BackendConnectionOptions opts{
                .host = kGatewayHost,
                .port = backend_port,
            };

            backend_conn = std::make_unique<v2::service::BackendConnection>(opts);
            if (!backend_conn->connect()) return false;

            acceptor = std::make_unique<tcp::acceptor>(
                io_context, tcp::endpoint(tcp::v4(), own_port));

            running = true;
            thread = std::thread([this] {
                do_accept();
                io_context.run();
            });
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void stop() {
        running = false;
        if (acceptor) {
            boost::system::error_code ec;
            acceptor->close(ec);
        }
        io_context.stop();
        if (thread.joinable()) thread.join();
        if (backend_conn) backend_conn->close();
    }

    [[nodiscard]] std::uint16_t local_port() const {
        if (!acceptor || !acceptor->is_open()) return 0;
        return acceptor->local_endpoint().port();
    }

private:
    void do_accept() {
        if (!running) return;
        auto socket = std::make_shared<tcp::socket>(io_context);
        acceptor->async_accept(*socket, [this, socket](boost::system::error_code ec) {
            if (!ec && running) handle_client(std::move(socket));
            do_accept();
        });
    }

    void handle_client(std::shared_ptr<tcp::socket> socket) {
        while (running && socket->is_open()) {
            // Read client length-prefixed packet
            net::packet::LengthHeader header{};
            boost::system::error_code ec;
            asio::read(*socket, asio::buffer(header),
                       asio::transfer_exactly(sizeof(header)), ec);
            if (ec) break;

            const auto payload_length = net::packet::decode_length(header);
            if (payload_length == 0 || payload_length > 1024 * 1024) break;

            std::vector<char> payload(payload_length);
            asio::read(*socket, asio::buffer(payload),
                       asio::transfer_exactly(payload_length), ec);
            if (ec) break;

            auto packet = net::packet::decode_payload(payload);

            // Map message_id to message_type string
            std::string message_type;
            switch (packet.message_id) {
                case net::protocol::kLoginRequest:
                    message_type = "login_request";
                    break;
                case 0:  // test: use reject_empty handler
                    message_type = "reject_empty";
                    break;
                default:
                    message_type = "unknown";
                    break;
            }

            // Forward to backend via BackendEnvelope
            v2::service::BackendEnvelope request{
                .target_service = v2::service::ServiceId::kLogin,
                .kind = v2::service::MessageKind::kRequest,
                .payload = packet.body,
                .message_type = message_type,
            };

            auto response = backend_conn->send_request(request);

            // Build client response packet
            if (response) {
                std::uint16_t response_msg_id;
                std::int32_t response_error = 0;
                std::string response_body;

                if (response->kind == v2::service::MessageKind::kError) {
                    response_msg_id = net::protocol::kErrorResponse;
                    response_error = response->error_code;
                    response_body = response->payload;
                } else {
                    response_msg_id = net::protocol::kLoginResponse;
                    response_body = response->payload;
                }

                auto outbound = net::packet::encode(
                    response_msg_id, packet.request_id, response_error, response_body);
                asio::write(*socket, asio::buffer(outbound), ec);
                if (ec) break;
            } else {
                // Backend timeout/unavailable
                auto outbound = net::packet::encode(
                    net::protocol::kErrorResponse, packet.request_id,
                    -2002, "backend_unavailable");
                asio::write(*socket, asio::buffer(outbound), ec);
                if (ec) break;
            }
        }
    }
};

// ─── TestClient (reuse pattern from demo_server_smoke_test) ───────

class TestClient {
public:
    TestClient() : socket_(io_context_) {}

    void connect(std::uint16_t port) {
        socket_.connect(tcp::endpoint(asio::ip::make_address(kGatewayHost), port));
    }

    void close() {
        boost::system::error_code ignored_ec;
        socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
        socket_.close(ignored_ec);
    }

    net::packet::DecodedPacket exchange(std::uint16_t message_id,
                                        std::uint32_t request_id,
                                        const std::string& body) {
        send(message_id, request_id, body);
        return read();
    }

    void send(std::uint16_t message_id, std::uint32_t request_id,
              const std::string& body) {
        auto outbound = net::packet::encode(message_id, request_id, 0, body);
        asio::write(socket_, asio::buffer(outbound));
    }

    net::packet::DecodedPacket read() {
        net::packet::LengthHeader header{};
        asio::read(socket_, asio::buffer(header));
        auto payload_length = net::packet::decode_length(header);

        std::vector<char> payload(payload_length);
        asio::read(socket_, asio::buffer(payload));
        return net::packet::decode_payload(payload);
    }

private:
    asio::io_context io_context_;
    tcp::socket socket_;
};

}  // namespace

// ─── Tests ────────────────────────────────────────────────────────

TEST(V2BackendRoutingTest, BasicRoundTrip) {
    app::logging::init("project_tests");

    BackendProcess backend;
    ASSERT_TRUE(backend.start());

    BridgeGateway gateway;
    ASSERT_TRUE(gateway.start(0, backend.port));

    TestClient client;
    client.connect(gateway.local_port());

    auto response = client.exchange(net::protocol::kLoginRequest, 1,
                                    "alice|token:alice_secret|Alice");
    EXPECT_EQ(response.message_id, net::protocol::kLoginResponse);
    EXPECT_EQ(response.body, "login_ok:alice");

    client.close();
    gateway.stop();
    backend.stop();
}

TEST(V2BackendRoutingTest, BackendUnreachable) {
    app::logging::init("project_tests");

    // Start backend first so gateway can connect, then stop it
    BackendProcess backend;
    ASSERT_TRUE(backend.start());

    BridgeGateway gateway;
    ASSERT_TRUE(gateway.start(0, backend.port));

    // Stop the backend to simulate crash/disconnect
    backend.stop();

    TestClient client;
    client.connect(gateway.local_port());

    auto response = client.exchange(net::protocol::kLoginRequest, 2,
                                    "bob|token:bob_secret|Bob");
    // Should get an error since backend is no longer running
    EXPECT_EQ(response.message_id, net::protocol::kErrorResponse);

    client.close();
    gateway.stop();
}

TEST(V2BackendRoutingTest, BackendReturnsError) {
    app::logging::init("project_tests");

    BackendProcess backend;
    ASSERT_TRUE(backend.start());

    BridgeGateway gateway;
    ASSERT_TRUE(gateway.start(0, backend.port));

    TestClient client;
    client.connect(gateway.local_port());

    // Send message with message_id=0 which maps to "reject_empty" handler
    auto response = client.exchange(0, 3, "");
    EXPECT_EQ(response.message_id, net::protocol::kErrorResponse);
    EXPECT_EQ(response.body, "rejected");

    client.close();
    gateway.stop();
    backend.stop();
}
