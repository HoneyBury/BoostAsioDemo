#pragma once

#include "v2/ecs/system.h"

namespace v2::battle {

class MovementSystem final : public v2::ecs::System {
public:
    void run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) override;
};

class CombatSystem final : public v2::ecs::System {
public:
    void run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) override;
};

}  // namespace v2::battle
