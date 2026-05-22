// EchoRealtimePlugin — a minimal InstancePlugin demo
//
// This plugin echoes every received input payload back in the next
// snapshot. It demonstrates the InstancePlugin SPI contract:
//   - on_instance_created: initialises state
//   - on_input: stores the latest input payload
//   - on_tick: advances internal frame counter (no simulation)
//   - build_snapshot: returns stored input payload as snapshot
//   - build_settlement: returns JSON with total frame count
//
// SPDX-License-Identifier: MIT

#pragma once

#include "v2/realtime/instance_plugin.h"

#include <string>

namespace echo_plugin {

// ─── EchoPlugin ─────────────────────────────────────────────────────
//
// Stores the most recent input payload per user and echoes it back
// in the next snapshot.

class EchoPlugin : public v2::realtime::InstancePlugin {
public:
    void on_instance_created(v2::realtime::InstanceContext& ctx) override;
    void on_player_join(v2::realtime::InstanceContext& ctx,
                        const v2::realtime::PlayerContext& player) override;
    void on_player_leave(v2::realtime::InstanceContext& ctx,
                         const v2::realtime::PlayerContext& player) override;
    v2::realtime::InputResult on_input(v2::realtime::InstanceContext& ctx,
                                        const v2::realtime::InputEnvelope& input) override;
    v2::realtime::TickStats on_tick(v2::realtime::InstanceContext& ctx,
                                     const v2::realtime::FrameContext& frame_ctx) noexcept override;
    v2::realtime::Snapshot build_snapshot(v2::realtime::InstanceContext& ctx,
                                           bool is_resume = false) noexcept override;
    std::string build_settlement(v2::realtime::InstanceContext& ctx,
                                  const v2::realtime::SettlementContext& settlement_ctx) noexcept override;
    v2::realtime::Snapshot build_resume_snapshot(v2::realtime::InstanceContext& ctx,
                                                  const v2::realtime::PlayerContext& player) noexcept override;

private:
    int tick_count_ = 0;
    std::string latest_input_payload_;
};

// Factory function for registration with InstanceRuntime
std::unique_ptr<v2::realtime::InstancePlugin> create_echo_plugin();

}  // namespace echo_plugin
