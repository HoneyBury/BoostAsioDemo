#include "v2/battle/game_systems.h"

#include "v2/battle/runtime_components.h"
#include "v2/ecs/world.h"

#include <algorithm>
#include <string>

namespace v2::battle {

namespace {
constexpr std::int32_t kMaxX = 1000;
constexpr std::int32_t kMaxY = 1000;
}  // namespace

void MovementSystem::run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) {
    (void)ctx;
    auto* simple_world = dynamic_cast<v2::ecs::SimpleWorld*>(&world);
    if (simple_world == nullptr) return;

    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle handle, BattleParticipantComponent& participant) {
            if (!participant.online || !participant.has_pending_move) return;

            auto* pos = simple_world->get_component<PositionComponent>(handle);
            if (pos == nullptr) return;

            pos->x = std::clamp(participant.pending_move_x, static_cast<std::int32_t>(0), kMaxX);
            pos->y = std::clamp(participant.pending_move_y, static_cast<std::int32_t>(0), kMaxY);

            participant.has_pending_move = false;
        });
}

void CombatSystem::run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) {
    (void)ctx;
    auto* simple_world = dynamic_cast<v2::ecs::SimpleWorld*>(&world);
    if (simple_world == nullptr) return;

    // Collect attack intents (source entity → target user_id)
    struct AttackIntent {
        v2::ecs::EntityHandle source;
        std::string target_user_id;
    };
    std::vector<AttackIntent> intents;

    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle handle, BattleParticipantComponent& participant) {
            if (!participant.online || participant.pending_target_user_id.empty()) return;
            intents.push_back({handle, participant.pending_target_user_id});
            participant.pending_target_user_id.clear();
        });

    // Resolve each attack intent
    for (const auto& intent : intents) {
        auto* source_pos = simple_world->get_component<PositionComponent>(intent.source);
        auto* source_attack = simple_world->get_component<AttackStateComponent>(intent.source);
        if (source_pos == nullptr || source_attack == nullptr) continue;

        // Find target entity by user_id
        simple_world->for_each<BattleParticipantComponent>(
            [&](v2::ecs::EntityHandle target_handle, BattleParticipantComponent& target_participant) {
                if (target_participant.user_id != intent.target_user_id) return;
                if (!target_participant.online) return;

                auto* target_pos = simple_world->get_component<PositionComponent>(target_handle);
                auto* target_health = simple_world->get_component<HealthComponent>(target_handle);
                if (target_pos == nullptr || target_health == nullptr) return;

                // Manhattan distance range check
                auto dx = std::abs(source_pos->x - target_pos->x);
                auto dy = std::abs(source_pos->y - target_pos->y);
                if (dx <= source_attack->range && dy <= source_attack->range) {
                    target_health->hp = std::max(
                        static_cast<std::int32_t>(0),
                        target_health->hp - source_attack->damage);
                }
            });
    }
}

}  // namespace v2::battle
