#pragma once

#include "v2/battle/message_types.h"
#include "v2/ecs/world.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace v2::battle {

[[nodiscard]] std::unique_ptr<v2::ecs::World> create_battle_world(
    const std::vector<std::string>& player_ids);

void battle_world_apply_input_score(v2::ecs::World& world,
                                    const std::string& user_id,
                                    std::int64_t score);

void battle_world_mark_offline(v2::ecs::World& world,
                               const std::string& user_id);

[[nodiscard]] std::uint32_t battle_world_tick(v2::ecs::World& world,
                                              const v2::ecs::FrameContext& ctx);

[[nodiscard]] std::vector<BattleScore> battle_world_collect_scores(
    v2::ecs::World& world,
    const std::vector<BattleParticipantState>& participants);

}  // namespace v2::battle
