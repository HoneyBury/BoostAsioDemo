#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "v2/actor/actor.h"
#include "v2/battle/message_types.h"
#include "v2/battle/runtime_world.h"
#include "v2/ecs/world.h"

namespace v2::battle {

class BattleEventSink {
public:
    virtual ~BattleEventSink() = default;

    virtual void push(BattleEvent event) = 0;
};

class BattleActor final : public v2::actor::Actor {
public:
    explicit BattleActor(BattleEventSink& sink)
        : sink_(sink) {}

    void on_message(v2::actor::Message&& message) override;

    [[nodiscard]] const BattleRuntimeState& state() const noexcept { return state_; }

private:
    void finish_battle(BattleFinishReason reason, std::string triggering_user_id);
    void sync_replay_inputs_from_world();
    void sync_state_from_world();

    BattleEventSink& sink_;
    BattleRuntimeState state_;
    std::unique_ptr<v2::ecs::World> world_;
};

}  // namespace v2::battle
