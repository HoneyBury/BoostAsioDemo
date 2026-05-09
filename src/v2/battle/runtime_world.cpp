#include "v2/battle/runtime_world.h"

#include "v2/battle/runtime_components.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace v2::battle {

namespace {

v2::ecs::SimpleWorld* as_simple_world(v2::ecs::World& world) {
    return dynamic_cast<v2::ecs::SimpleWorld*>(&world);
}

}  // namespace

void AdvanceFrameSystem::run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) {
    auto* simple_world = dynamic_cast<v2::ecs::SimpleWorld*>(&world);
    if (simple_world == nullptr) {
        return;
    }
    simple_world->for_each<BattleClockComponent>(
        [&](v2::ecs::EntityHandle, BattleClockComponent& clock) {
            clock.frame_number = ctx.frame_number;
            clock.last_trigger = ctx.trigger;
        });
}

std::unique_ptr<v2::ecs::World> create_battle_world(const std::vector<std::string>& player_ids) {
    auto world = std::make_unique<v2::ecs::SimpleWorld>();
    world->add_system(std::make_unique<AdvanceFrameSystem>());

    const auto clock_entity = world->create_entity();
    world->add_component<BattleClockComponent>(clock_entity);

    for (const auto& user_id : player_ids) {
        const auto entity = world->create_entity();
        auto& participant = world->add_component<BattleParticipantComponent>(entity);
        participant.user_id = user_id;
    }

    return world;
}

void battle_world_apply_input_score(v2::ecs::World& world,
                                    const std::string& user_id,
                                    std::int64_t score) {
    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return;
    }
    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& participant) {
            if (participant.user_id == user_id) {
                participant.score += score;
            }
        });
}

void battle_world_mark_offline(v2::ecs::World& world,
                               const std::string& user_id) {
    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return;
    }
    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& participant) {
            if (participant.user_id == user_id) {
                participant.online = false;
            }
        });
}

std::uint32_t battle_world_tick(v2::ecs::World& world,
                                const v2::ecs::FrameContext& ctx) {
    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return 0;
    }
    world.tick(ctx);
    std::uint32_t frame_number = 0;
    simple_world->for_each<BattleClockComponent>(
        [&](v2::ecs::EntityHandle, BattleClockComponent& clock) {
            frame_number = clock.frame_number;
        });
    return frame_number;
}

std::vector<BattleScore> battle_world_collect_scores(
    v2::ecs::World& world,
    const std::vector<BattleParticipantState>& participants) {
    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return {};
    }

    std::unordered_map<std::string, std::int64_t> scores_by_user_id;
    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& participant) {
            scores_by_user_id[participant.user_id] = participant.score;
        });

    std::vector<BattleScore> scores;
    scores.reserve(participants.size());
    for (const auto& participant : participants) {
        scores.push_back(BattleScore{
            .user_id = participant.user_id,
            .score = scores_by_user_id[participant.user_id],
        });
    }
    return scores;
}

BattleWorldSnapshot battle_world_snapshot(v2::ecs::World& world) {
    BattleWorldSnapshot snapshot;

    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return snapshot;
    }

    simple_world->for_each<BattleClockComponent>(
        [&](v2::ecs::EntityHandle, BattleClockComponent& clock) {
            snapshot.clock.frame_number = clock.frame_number;
            snapshot.clock.last_trigger = clock.last_trigger;
        });

    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& participant) {
            snapshot.participants.push_back(BattleWorldParticipantState{
                .user_id = participant.user_id,
                .online = participant.online,
                .score = participant.score,
            });
        });

    return snapshot;
}

}  // namespace v2::battle
