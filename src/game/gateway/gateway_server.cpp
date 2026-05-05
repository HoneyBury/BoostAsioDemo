#include "game/gateway/gateway_server.h"

#include "app/logging.h"

#include <utility>

namespace game::gateway {

GatewayServer::GatewayServer(asio::io_context& io_context,
                             net::MessageDispatcher& dispatcher,
                             SessionManager& session_manager,
                             GatewayMetrics& metrics,
                             std::uint16_t port,
                             net::SessionOptions session_options,
                             std::chrono::milliseconds metrics_log_interval)
    : io_context_(io_context),
      dispatcher_(dispatcher),
      session_manager_(session_manager),
      metrics_(metrics),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
      metrics_timer_(io_context),
      session_options_(std::move(session_options)),
      metrics_log_interval_(metrics_log_interval) {}

void GatewayServer::start() {
    LOG_INFO("Gateway server listening on 0.0.0.0:{}", acceptor_.local_endpoint().port());
    arm_metrics_timer();
    do_accept();
}

void GatewayServer::stop() {
    error_code ignored_ec;
    acceptor_.close(ignored_ec);
    metrics_timer_.cancel();

    for (auto& session : session_manager_.all_sessions()) {
        session->stop();
    }
}

std::uint16_t GatewayServer::local_port() const {
    return acceptor_.local_endpoint().port();
}

void GatewayServer::do_accept() {
    acceptor_.async_accept([this](const error_code& ec, tcp::socket socket) {
        if (ec) {
            if (ec != asio::error::operation_aborted) {
                LOG_ERROR("Accept failed: {}", ec.message());
            }
            return;
        }

        auto session = std::make_shared<net::Session>(std::move(socket), session_options_);
        session_manager_.add_session(session);
        metrics_.on_session_accepted();

        LOG_INFO("Accepted client {}", session->remote_endpoint());

        session->set_receive_observer(
            [this](const std::shared_ptr<net::Session>&,
                   std::uint16_t,
                   std::size_t body_size) { metrics_.on_packet_received(body_size); });

        session->set_send_observer(
            [this](const std::shared_ptr<net::Session>&,
                   std::uint16_t,
                   std::size_t body_size) { metrics_.on_packet_sent(body_size); });

        session->set_packet_handler(
            [this](const std::shared_ptr<net::Session>& session_ptr,
                   std::uint16_t message_id,
                   std::string body) { dispatcher_.dispatch(session_ptr, message_id, std::move(body)); });

        session->set_close_handler(
            [this](const std::shared_ptr<net::Session>& session_ptr, const error_code&) {
                session_manager_.remove_session(session_ptr);
                metrics_.on_session_closed();
            });

        session->start();
        do_accept();
    });
}

void GatewayServer::arm_metrics_timer() {
    metrics_timer_.expires_after(metrics_log_interval_);
    metrics_timer_.async_wait([this](const error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return;
        }

        if (ec) {
            LOG_WARN("Metrics timer failed: {}", ec.message());
            return;
        }

        const auto session_snapshot = session_manager_.snapshot();
        LOG_INFO("Gateway metrics: {}, active_sessions={}, authenticated_sessions={}, rooms={}, battles={}",
                 metrics_.summary(),
                 session_snapshot.active_sessions,
                 session_snapshot.authenticated_sessions,
                 session_snapshot.active_rooms,
                 session_snapshot.active_battles);

        arm_metrics_timer();
    });
}

}  // namespace game::gateway
