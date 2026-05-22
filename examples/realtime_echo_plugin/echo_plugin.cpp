// EchoRealtimePlugin implementation

#include "echo_plugin.h"

#include <nlohmann/json.hpp>

namespace echo_plugin {

void EchoPlugin::on_instance_created(v2::realtime::InstanceContext& ctx) {
    ctx.plugin_state = this;
    tick_count_ = 0;
}

void EchoPlugin::on_player_join(v2::realtime::InstanceContext& /*ctx*/,
                                 const v2::realtime::PlayerContext& /*player*/) {
    // No special handling for player join in echo demo
}

void EchoPlugin::on_player_leave(v2::realtime::InstanceContext& /*ctx*/,
                                  const v2::realtime::PlayerContext& /*player*/) {
    // No special handling for player leave in echo demo
}

v2::realtime::InputResult EchoPlugin::on_input(
    v2::realtime::InstanceContext& /*ctx*/,
    const v2::realtime::InputEnvelope& input) {
    latest_input_payload_ = input.payload;
    return v2::realtime::InputResult{.accepted = true, .ack_seq = 1};
}

v2::realtime::TickStats EchoPlugin::on_tick(
    v2::realtime::InstanceContext& /*ctx*/,
    const v2::realtime::FrameContext& frame_ctx) noexcept {
    tick_count_++;

    v2::realtime::TickStats stats;
    stats.frame_number = frame_ctx.frame_number;
    stats.inputs_processed = static_cast<std::uint32_t>(frame_ctx.inputs_this_tick.size());
    stats.tick_duration_ms = 0.1;
    return stats;
}

v2::realtime::Snapshot EchoPlugin::build_snapshot(
    v2::realtime::InstanceContext& /*ctx*/, bool is_resume) noexcept {
    v2::realtime::Snapshot snap;
    snap.payload_type = "echo.snapshot";

    nlohmann::json j;
    j["tick"] = tick_count_;
    j["echo"] = latest_input_payload_;
    j["is_resume"] = is_resume;
    snap.payload = j.dump();
    snap.is_full = true;
    snap.is_resume = is_resume;
    return snap;
}

std::string EchoPlugin::build_settlement(
    v2::realtime::InstanceContext& /*ctx*/,
    const v2::realtime::SettlementContext& settlement_ctx) noexcept {
    nlohmann::json j;
    j["status"] = "ok";
    j["total_frames"] = settlement_ctx.total_frames;
    j["plugin_type"] = "echo";
    return j.dump();
}

v2::realtime::Snapshot EchoPlugin::build_resume_snapshot(
    v2::realtime::InstanceContext& /*ctx*/,
    const v2::realtime::PlayerContext& player) noexcept {
    v2::realtime::Snapshot snap;
    snap.payload_type = "echo.resume";
    snap.payload = R"({"user":")" + player.user_id + R"(","tick":)" + std::to_string(tick_count_) + "}";
    snap.frame_number = tick_count_;
    snap.is_resume = true;
    return snap;
}

// ─── Factory ────────────────────────────────────────────────────────

std::unique_ptr<v2::realtime::InstancePlugin> create_echo_plugin() {
    return std::make_unique<EchoPlugin>();
}

}  // namespace echo_plugin
