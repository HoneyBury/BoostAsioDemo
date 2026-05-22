#pragma once

#include "v2/realtime/types.h"

#include <memory>
#include <string>
#include <vector>

namespace v2::realtime {

// ─── Business Plugin SPI ────────────────────────────────────────────
//
// Implement this interface to attach business logic to the realtime
// instance runtime. The runtime controls lifecycle and scheduling;
// the plugin only implements domain-specific rules.

class InstancePlugin {
public:
    virtual ~InstancePlugin() = default;

    // Called when a new instance is created. The plugin should set up
    // its world state and store it via instance_ctx.plugin_state.
    virtual void on_instance_created(InstanceContext& instance_ctx) = 0;

    // Called when a player joins an existing instance.
    virtual void on_player_join(InstanceContext& instance_ctx,
                                const PlayerContext& player) = 0;

    // Called when a player leaves or disconnects.
    virtual void on_player_leave(InstanceContext& instance_ctx,
                                 const PlayerContext& player) = 0;

    // Process a single input. Return accepted=false to reject.
    // The plugin should update its world state as needed.
    virtual InputResult on_input(InstanceContext& instance_ctx,
                                 const InputEnvelope& input) = 0;

    // Advance the simulation by one tick. Return collected pushes and
    // whether the instance should finish.
    virtual TickStats on_tick(InstanceContext& instance_ctx,
                              const FrameContext& frame_ctx) = 0;

    // Build a current snapshot of the instance state.
    virtual Snapshot build_snapshot(InstanceContext& instance_ctx,
                                    bool is_resume = false) = 0;

    // Build settlement payload when the instance finishes.
    virtual std::string build_settlement(InstanceContext& instance_ctx,
                                         const SettlementContext& settlement_ctx) = 0;

    // Build a resume snapshot for a reconnecting player.
    virtual Snapshot build_resume_snapshot(InstanceContext& instance_ctx,
                                           const PlayerContext& player) = 0;
};

// ─── Factory type ───────────────────────────────────────────────────

using InstancePluginFactory = std::unique_ptr<InstancePlugin>(*)();

}  // namespace v2::realtime
