#include "game/gateway/gateway_metrics.h"

#include <fmt/format.h>

namespace game::gateway {

void GatewayMetrics::on_session_accepted() {
    accepted_sessions_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayMetrics::on_session_closed() {
    closed_sessions_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayMetrics::on_packet_received(std::size_t body_size) {
    received_packets_.fetch_add(1, std::memory_order_relaxed);
    received_bytes_.fetch_add(body_size, std::memory_order_relaxed);
}

void GatewayMetrics::on_packet_sent(std::size_t body_size) {
    sent_packets_.fetch_add(1, std::memory_order_relaxed);
    sent_bytes_.fetch_add(body_size, std::memory_order_relaxed);
}

void GatewayMetrics::on_packet_blocked() {
    blocked_packets_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayMetrics::on_login_success() {
    login_successes_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayMetrics::on_room_join_success() {
    room_join_successes_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayMetrics::on_battle_start_success() {
    battle_start_successes_.fetch_add(1, std::memory_order_relaxed);
}

GatewayMetricsSnapshot GatewayMetrics::snapshot() const {
    return GatewayMetricsSnapshot{
        .accepted_sessions = accepted_sessions_.load(std::memory_order_relaxed),
        .closed_sessions = closed_sessions_.load(std::memory_order_relaxed),
        .received_packets = received_packets_.load(std::memory_order_relaxed),
        .sent_packets = sent_packets_.load(std::memory_order_relaxed),
        .received_bytes = received_bytes_.load(std::memory_order_relaxed),
        .sent_bytes = sent_bytes_.load(std::memory_order_relaxed),
        .blocked_packets = blocked_packets_.load(std::memory_order_relaxed),
        .login_successes = login_successes_.load(std::memory_order_relaxed),
        .room_join_successes = room_join_successes_.load(std::memory_order_relaxed),
        .battle_start_successes = battle_start_successes_.load(std::memory_order_relaxed),
    };
}

std::string GatewayMetrics::summary() const {
    const auto current = snapshot();
    return fmt::format(
        "accepted={}, closed={}, recv_packets={}, sent_packets={}, recv_bytes={}, sent_bytes={}, "
        "blocked={}, login_ok={}, room_ok={}, battle_ok={}",
        current.accepted_sessions,
        current.closed_sessions,
        current.received_packets,
        current.sent_packets,
        current.received_bytes,
        current.sent_bytes,
        current.blocked_packets,
        current.login_successes,
        current.room_join_successes,
        current.battle_start_successes);
}

GatewayMetricsRateSnapshot GatewayMetrics::compute_rates(const GatewayMetricsSnapshot& current,
                                                           const GatewayMetricsSnapshot& previous,
                                                           double elapsed_sec) {
    if (elapsed_sec <= 0.0) {
        return {};
    }
    return GatewayMetricsRateSnapshot{
        .accepted_sessions_per_sec =
            static_cast<double>(current.accepted_sessions - previous.accepted_sessions) / elapsed_sec,
        .closed_sessions_per_sec =
            static_cast<double>(current.closed_sessions - previous.closed_sessions) / elapsed_sec,
        .received_packets_per_sec =
            static_cast<double>(current.received_packets - previous.received_packets) / elapsed_sec,
        .sent_packets_per_sec =
            static_cast<double>(current.sent_packets - previous.sent_packets) / elapsed_sec,
        .received_bytes_per_sec =
            static_cast<double>(current.received_bytes - previous.received_bytes) / elapsed_sec,
        .sent_bytes_per_sec =
            static_cast<double>(current.sent_bytes - previous.sent_bytes) / elapsed_sec,
        .blocked_packets_per_sec =
            static_cast<double>(current.blocked_packets - previous.blocked_packets) / elapsed_sec,
        .login_successes_per_sec =
            static_cast<double>(current.login_successes - previous.login_successes) / elapsed_sec,
        .room_join_successes_per_sec =
            static_cast<double>(current.room_join_successes - previous.room_join_successes) / elapsed_sec,
        .battle_start_successes_per_sec =
            static_cast<double>(current.battle_start_successes - previous.battle_start_successes) / elapsed_sec,
    };
}

}  // namespace game::gateway
