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

}  // namespace game::gateway
