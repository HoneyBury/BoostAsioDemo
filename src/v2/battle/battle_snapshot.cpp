#include "v2/battle/battle_snapshot.h"

#include "v2/battle/runtime_components.h"
#include "v2/ecs/world.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace v2::battle {

namespace {

v2::ecs::SimpleWorld* as_simple_world(v2::ecs::World& world) {
    return dynamic_cast<v2::ecs::SimpleWorld*>(&world);
}

}  // namespace

std::string battle_world_snapshot_to_json(v2::ecs::World& world) {
    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return "{}";
    }

    nlohmann::json doc;

    // Clock
    simple_world->for_each<BattleClockComponent>(
        [&](v2::ecs::EntityHandle, BattleClockComponent& clock) {
            doc["clock"] = {
                {"frame_number", clock.frame_number},
                {"last_trigger", clock.last_trigger},
            };
        });

    // Metadata
    simple_world->for_each<BattleMetadataComponent>(
        [&](v2::ecs::EntityHandle, BattleMetadataComponent& metadata) {
            doc["metadata"] = {
                {"battle_id", metadata.battle_id},
                {"room_id", metadata.room_id},
                {"lifecycle", static_cast<int>(metadata.lifecycle)},
                {"frame_number", metadata.current_frame_number},
                {"max_frames", metadata.max_frames},
                {"next_input_seq", metadata.next_input_seq},
            };
        });

    // Participants
    nlohmann::json participants = nlohmann::json::array();
    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& participant) {
            participants.push_back({
                {"user_id", participant.user_id},
                {"online", participant.online},
                {"score", participant.score},
                {"last_submitted_frame", participant.last_submitted_frame},
                {"last_acked_frame", participant.last_acked_frame},
            });
        });
    doc["participants"] = std::move(participants);

    // Replay inputs
    nlohmann::json replay_inputs = nlohmann::json::array();
    simple_world->for_each<BattleReplayLogComponent>(
        [&](v2::ecs::EntityHandle, BattleReplayLogComponent& replay_log) {
            for (const auto& r : replay_log.replay_inputs) {
                replay_inputs.push_back({
                    {"input_seq", r.input_seq},
                    {"frame_number", r.frame_number},
                    {"user_id", r.user_id},
                    {"input_data", r.input_data},
                    {"score", r.score},
                    {"trigger", r.trigger},
                });
            }
        });
    doc["replay_inputs"] = std::move(replay_inputs);

    return doc.dump();
}

bool battle_world_restore_from_json(v2::ecs::World& world, std::string_view json) {
    auto doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return false;
    }

    auto* simple_world = as_simple_world(world);
    if (simple_world == nullptr) {
        return false;
    }

    if (!doc.contains("clock") || !doc.contains("metadata") ||
        !doc.contains("participants") || !doc.contains("replay_inputs")) {
        return false;
    }

    // Restore clock
    const auto& clock_json = doc["clock"];
    simple_world->for_each<BattleClockComponent>(
        [&](v2::ecs::EntityHandle, BattleClockComponent& clock) {
            clock.frame_number = clock_json.value("frame_number", 0U);
            clock.last_trigger = clock_json.value("last_trigger", "");
        });

    // Restore metadata
    const auto& metadata_json = doc["metadata"];
    simple_world->for_each<BattleMetadataComponent>(
        [&](v2::ecs::EntityHandle, BattleMetadataComponent& metadata) {
            metadata.battle_id = metadata_json.value("battle_id", "");
            metadata.room_id = metadata_json.value("room_id", "");
            metadata.lifecycle = static_cast<BattleLifecycleState>(
                metadata_json.value("lifecycle", 0));
            metadata.current_frame_number = metadata_json.value("frame_number", 0U);
            metadata.max_frames = metadata_json.value("max_frames", 0U);
            metadata.next_input_seq = metadata_json.value("next_input_seq", 1ULL);
        });

    // Restore participants
    const auto& participants_json = doc["participants"];
    std::vector<BattleParticipantComponent*> participants;
    simple_world->for_each<BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle, BattleParticipantComponent& comp) {
            participants.push_back(&comp);
        });

    if (participants.size() != participants_json.size()) {
        return false;
    }

    for (std::size_t i = 0; i < participants.size(); ++i) {
        const auto& p = participants_json[i];
        auto* comp = participants[i];
        comp->user_id = p.value("user_id", "");
        comp->online = p.value("online", true);
        comp->score = p.value("score", 0);
        comp->last_submitted_frame = p.value("last_submitted_frame", 0U);
        comp->last_acked_frame = p.value("last_acked_frame", 0U);
    }

    // Restore replay log
    const auto& replay_json = doc["replay_inputs"];
    simple_world->for_each<BattleReplayLogComponent>(
        [&](v2::ecs::EntityHandle, BattleReplayLogComponent& replay_log) {
            replay_log.replay_inputs.clear();
            for (const auto& r : replay_json) {
                replay_log.replay_inputs.push_back(BattleReplayInputRecord{
                    .input_seq = r.value("input_seq", 0ULL),
                    .frame_number = r.value("frame_number", 0U),
                    .user_id = r.value("user_id", ""),
                    .input_data = r.value("input_data", ""),
                    .score = r.value("score", 0),
                    .trigger = r.value("trigger", ""),
                });
            }
        });

    return true;
}

}  // namespace v2::battle
