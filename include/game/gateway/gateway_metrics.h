#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace game::gateway {

struct GatewayMetricsSnapshot {
    std::uint64_t accepted_sessions = 0;
    std::uint64_t closed_sessions = 0;
    std::uint64_t received_packets = 0;
    std::uint64_t sent_packets = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t sent_bytes = 0;
    std::uint64_t blocked_packets = 0;
    std::uint64_t login_successes = 0;
    std::uint64_t room_join_successes = 0;
    std::uint64_t battle_start_successes = 0;
};

struct GatewayMetricsRateSnapshot {
    double accepted_sessions_per_sec = 0.0;
    double closed_sessions_per_sec = 0.0;
    double received_packets_per_sec = 0.0;
    double sent_packets_per_sec = 0.0;
    double received_bytes_per_sec = 0.0;
    double sent_bytes_per_sec = 0.0;
    double blocked_packets_per_sec = 0.0;
    double login_successes_per_sec = 0.0;
    double room_join_successes_per_sec = 0.0;
    double battle_start_successes_per_sec = 0.0;
};

class GatewayMetrics {
public:
    void on_session_accepted();
    void on_session_closed();
    void on_packet_received(std::size_t body_size);
    void on_packet_sent(std::size_t body_size);
    void on_packet_blocked();
    void on_login_success();
    void on_room_join_success();
    void on_battle_start_success();

    [[nodiscard]] GatewayMetricsSnapshot snapshot() const;
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] static GatewayMetricsRateSnapshot compute_rates(const GatewayMetricsSnapshot& current,
                                                                   const GatewayMetricsSnapshot& previous,
                                                                   double elapsed_sec);

private:
    std::atomic<std::uint64_t> accepted_sessions_{0};
    std::atomic<std::uint64_t> closed_sessions_{0};
    std::atomic<std::uint64_t> received_packets_{0};
    std::atomic<std::uint64_t> sent_packets_{0};
    std::atomic<std::uint64_t> received_bytes_{0};
    std::atomic<std::uint64_t> sent_bytes_{0};
    std::atomic<std::uint64_t> blocked_packets_{0};
    std::atomic<std::uint64_t> login_successes_{0};
    std::atomic<std::uint64_t> room_join_successes_{0};
    std::atomic<std::uint64_t> battle_start_successes_{0};
};

}  // namespace game::gateway
