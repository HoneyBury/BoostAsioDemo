#pragma once

#include "v2/ecs/component.h"
#include "v2/ecs/system.h"

#include <cstdint>
#include <string>

namespace v2::battle {

struct BattleClockComponent final : v2::ecs::Component {
    std::uint32_t frame_number = 0;
    std::string last_trigger;
};

struct BattleParticipantComponent final : v2::ecs::Component {
    std::string user_id;
    bool online = true;
    std::int64_t score = 0;
};

class AdvanceFrameSystem final : public v2::ecs::System {
public:
    void run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) override;
};

}  // namespace v2::battle
