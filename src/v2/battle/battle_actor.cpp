#include "v2/battle/battle_actor.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace v2::battle {

BattleLifecycleState BattleActor::lifecycle() const {
    if (world_ == nullptr) {
        return BattleLifecycleState::kCreated;
    }
    return battle_world_lifecycle(*world_);
}

std::vector<BattleParticipantState> BattleActor::participants() const {
    if (world_ == nullptr) {
        return {};
    }
    return battle_world_participants(*world_);
}

std::vector<BattleReplayInputRecord> BattleActor::replay_inputs() const {
    if (world_ == nullptr) {
        return {};
    }
    return battle_world_collect_replay_inputs(*world_);
}

BattleRuntimeState BattleActor::state() const {
    return BattleRuntimeState{
        .battle_id = battle_id_,
        .room_id = room_id_,
        .lifecycle = lifecycle(),
        .participants = participants(),
        .frame_number = frame_number_,
        .replay_inputs = replay_inputs(),
    };
}

void BattleActor::finish_battle(BattleFinishReason reason, std::string triggering_user_id) {
    const auto current_state = state();
    if (current_state.lifecycle == BattleLifecycleState::kFinished) {
        return;
    }

    if (world_ != nullptr) {
        battle_world_set_lifecycle(*world_, BattleLifecycleState::kFinished);
    }
    const auto finished_state = state();
    std::vector<std::string> participant_user_ids;
    participant_user_ids.reserve(finished_state.participants.size());
    for (const auto& participant : finished_state.participants) {
        participant_user_ids.push_back(participant.user_id);
    }

    BattleResultSummary result;
    if (world_ != nullptr) {
        result = battle_world_build_result_summary(
            *world_, battle_id_, room_id_, finished_state.participants, reason, frame_number_);
    } else {
        result = BattleResultSummary{
            .battle_id = battle_id_,
            .room_id = room_id_,
            .reason = reason,
            .winner_user_id = std::nullopt,
            .scores = {},
            .total_frames = frame_number_,
        };
    }

    sink_.push(BattleSettlementPreparedMsg{
        .battle_id = battle_id_,
        .room_id = room_id_,
        .reason = reason,
        .triggering_user_id = triggering_user_id,
        .total_frames = frame_number_,
        .participant_user_ids = std::move(participant_user_ids),
        .replay_inputs = finished_state.replay_inputs,
        .result = std::move(result),
    });
    sink_.push(BattleFinishedMsg{
        .battle_id = battle_id_,
        .room_id = room_id_,
        .reason = reason,
        .triggering_user_id = std::move(triggering_user_id),
    });
}

void BattleActor::on_message(v2::actor::Message&& message) {
    if (const auto* create = std::get_if<CreateBattleMsg>(&message.payload)) {
        battle_id_ = create->battle_id;
        room_id_ = create->room_id;
        frame_number_ = 0;
        world_ = create_battle_world(create->player_ids, create->max_frames);

        sink_.push(BattleCreatedMsg{
            .battle_id = battle_id_,
            .room_id = room_id_,
            .player_ids = create->player_ids,
        });
        return;
    }

    const auto* input = std::get_if<SubmitBattleInputMsg>(&message.payload);
    if (input != nullptr && lifecycle() == BattleLifecycleState::kRunning) {
        if (world_ != nullptr && !battle_world_should_accept_input(*world_, input->user_id, input->submitted_frame)) {
            return;
        }
        if (world_ != nullptr && input->submitted_frame > 0) {
            battle_world_record_submitted_frame(*world_, input->user_id, input->submitted_frame);
        }
        std::uint64_t input_seq = 0;
        if (world_ != nullptr) {
            battle_world_apply_input_score(*world_, input->user_id, input->score);
            input_seq = battle_world_append_replay_input(
                *world_, frame_number_ + 1, input->user_id, input->input_data, input->score);
        }
        sink_.push(BattleInputAcceptedMsg{
            .battle_id = battle_id_,
            .room_id = room_id_,
            .user_id = input->user_id,
            .input_seq = input_seq,
            .request_id = input->request_id,
            .input_data = input->input_data,
        });
        return;
    }

    const auto* tick = std::get_if<TickBattleMsg>(&message.payload);
    if (tick != nullptr && lifecycle() == BattleLifecycleState::kRunning) {
        if (world_ != nullptr) {
            frame_number_ = battle_world_tick(*world_, v2::ecs::FrameContext{
                .battle_id = battle_id_,
                .room_id = room_id_,
                .frame_number = frame_number_ + 1,
                .trigger = tick->trigger,
            });
        } else {
            ++frame_number_;
        }
        if (world_ != nullptr) {
            battle_world_apply_trigger_to_frame(*world_, frame_number_, tick->trigger);
        }
        sink_.push(BattleFrameAdvancedMsg{
            .battle_id = battle_id_,
            .room_id = room_id_,
            .frame_number = frame_number_,
            .trigger = tick->trigger,
        });
        if (world_ != nullptr && battle_world_should_finish_for_frame_limit(*world_, frame_number_)) {
            finish_battle(BattleFinishReason::kFrameLimitReached, tick->trigger);
        }
        return;
    }

    const auto* end = std::get_if<EndBattleMsg>(&message.payload);
    if (end != nullptr && lifecycle() == BattleLifecycleState::kRunning) {
        finish_battle(end->reason, end->triggering_user_id);
        return;
    }

    const auto* ack = std::get_if<FrameAckMsg>(&message.payload);
    if (ack != nullptr) {
        if (world_ != nullptr) {
            battle_world_record_frame_ack(*world_, ack->user_id, ack->frame_number);
        }
        sink_.push(*ack);
        return;
    }

    const auto* disconnected = std::get_if<PlayerDisconnectedMsg>(&message.payload);
    if (disconnected == nullptr || lifecycle() != BattleLifecycleState::kRunning) {
        return;
    }

    if (world_ == nullptr) {
        return;
    }
    if (!battle_world_mark_offline(*world_, disconnected->user_id)) {
        return;
    }
    finish_battle(BattleFinishReason::kPlayerDisconnected, disconnected->user_id);
}

}  // namespace v2::battle
