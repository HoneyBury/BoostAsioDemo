#pragma once

#include "game/gateway/gateway_metrics_exporter.h"
#include "game/gateway/gateway_metrics.h"
#include "game/gateway/session_manager.h"
#include "game/room/room_manager.h"
#include "game/battle/battle_manager.h"
#include "net/http_manager.h"
#include "net/message_dispatcher.h"
#include "net/session.h"
#include "v2/io/io_engine.h"

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game::gateway {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

class GatewayPacketBridge {
public:
    virtual ~GatewayPacketBridge() = default;

    virtual void on_packet(const std::shared_ptr<net::Session>& session,
                           const net::Session::PacketMessage& message) = 0;
    virtual void on_close(const std::shared_ptr<net::Session>& session) = 0;
};

class GatewayServer {
public:
    GatewayServer(asio::io_context& io_context,
                  net::MessageDispatcher& dispatcher,
                  SessionManager& session_manager,
                  game::room::RoomManager& room_manager,
                  game::battle::BattleManager& battle_manager,
                  GatewayMetrics& metrics,
                  std::uint16_t port,
                  std::uint16_t http_management_port = 0,
                  net::SessionOptions session_options = {},
                  std::chrono::milliseconds metrics_log_interval = std::chrono::milliseconds(5000),
                  GatewayMetricsExportOptions metrics_export_options = {},
                  std::unique_ptr<v2::io::IoEngine> io_engine = nullptr);

    void start();
    void stop();
    bool attach_session(const std::shared_ptr<net::Session>& session);
    void set_connection_limits(std::size_t max_total, std::size_t per_ip);
    void set_packet_bridge(std::shared_ptr<GatewayPacketBridge> packet_bridge);
    [[nodiscard]] std::size_t active_connections() const;
    [[nodiscard]] std::uint16_t local_port() const;

private:
    void do_accept();
    void do_accept_with_io_engine();
    void arm_metrics_timer();
    bool release_session_state(const std::shared_ptr<net::Session>& session,
                               std::string_view client_ip);
    [[nodiscard]] bool try_acquire_connection_slot(std::string_view client_ip);
    void release_connection_slot(std::string_view client_ip);
    [[nodiscard]] static std::string extract_ip(std::string_view remote_endpoint);

    net::MessageDispatcher& dispatcher_;
    SessionManager& session_manager_;
    game::room::RoomManager& room_manager_;
    game::battle::BattleManager& battle_manager_;
    GatewayMetrics& metrics_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    asio::steady_timer metrics_timer_;
    net::SessionOptions session_options_;
    std::chrono::milliseconds metrics_log_interval_;
    GatewayMetricsExportOptions metrics_export_options_;
    std::unique_ptr<net::HttpManager> http_manager_;
    std::unique_ptr<v2::io::IoEngine> io_engine_;
    std::unique_ptr<v2::io::IoAcceptor> io_acceptor_;
    GatewayMetricsSnapshot previous_metrics_snapshot_;
    std::chrono::steady_clock::time_point last_metrics_export_time_;
    std::size_t max_connections_ = 0;
    std::size_t per_ip_limit_ = 0;
    std::atomic<std::size_t> active_connection_count_{0};
    std::mutex ip_count_mutex_;
    std::unordered_map<std::string, std::size_t> ip_connection_counts_;
    std::shared_ptr<GatewayPacketBridge> packet_bridge_;
};

}  // namespace game::gateway
