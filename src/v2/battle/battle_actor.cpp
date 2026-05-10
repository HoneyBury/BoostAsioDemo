#include "v2/battle/battle_actor.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace v2::battle {

void BattleActor::sync_replay_inputs_from_world() {
    if (world_ == nullptr) {
        return;
    }
    state_.replay_inputs = battle_world_collect_replay_inputs(*world_);
}

void BattleActor::sync_state_from_world() {
    if (world_ == nullptr) {
        return;
    }
    state_.lifecycle = battle_world_lifecycle(*world_);
    state_.participants = battle_world_participants(*world_);
}

void BattleActor::finish_battle(BattleFinishReason reason, std::string triggering_user_id) {
    if (state_.lifecycle == BattleLifecycleState::kFinished) {
        return;
    }

    if (world_ != nullptr) {
        battle_world_set_lifecycle(*world_, BattleLifecycleState::kFinished);
    }
    sync_state_from_world();
    std::vector<std::string> participant_user_ids;
    participant_user_ids.reserve(state_.participants.size());
    for (const auto& participant : state_.participants) {
        participant_user_ids.push_back(participant.user_id);
    }

    sync_replay_inputs_from_world();

    BattleResultSummary result;
    if (world_ != nullptr) {
        result = battle_world_build_result_summary(
            *world_, state_.battle_id, state_.room_id, state_.participants, reason, state_.frame_number);
    } else {
        std::vector<BattleScore> scores;
        scores.reserve(state_.participants.size());
        std::optional<std::string> winner_user_id;
        std::int64_t high_score = 0;
        bool any_score_set = false;
        for (const auto& participant : state_.participants) {
            std::int64_t score = 0;
            for (const auto& input : state_.replay_inputs) {
                if (input.user_id == participant.user_id) {
                    score += input.score;
                }
            }
            scores.push_back(BattleScore{.user_id = participant.user_id, .score = score});
            if (!any_score_set || score > high_score) {
                any_score_set = true;
                high_score = score;
                winner_user_id = participant.user_id;
            }
        }
        result = BattleResultSummary{
            .battle_id = state_.battle_id,
            .room_id = state_.room_id,
            .reason = reason,
            .winner_user_id = winner_user_id,
            .scores = std::move(scores),
            .total_frames = state_.frame_number,
        };
    }

    sink_.push(BattleSettlementPreparedMsg{
        .battle_id = state_.battle_id,
        .room_id = state_.room_id,
        .reason = reason,
        .triggering_user_id = triggering_user_id,
        .total_frames = state_.frame_number,
        .participant_user_ids = std::move(participant_user_ids),
        .replay_inputs = state_.replay_inputs,
        .result = std::move(result),
    });
    sink_.push(BattleFinishedMsg{
        .battle_id = state_.battle_id,
        .room_id = state_.room_id,
        .reason = reason,
        .triggering_user_id = std::move(triggering_user_id),
    });
}

void BattleActor::on_message(v2::actor::Message&& message) {
    if (const auto* create = std::get_if<CreateBattleMsg>(&message.payload)) {
        state_.battle_id = create->battle_id;
        state_.room_id = create->room_id;
        state_.frame_number = 0;
        state_.replay_inputs.clear();
        world_ = create_battle_world(create->player_ids, create->max_frames);
        sync_state_from_world();

        sink_.push(BattleCreatedMsg{
            .battle_id = state_.battle_id,
            .room_id = state_.room_id,
            .player_ids = create->player_ids,
        });
        return;
    }

    const auto* input = std::get_if<SubmitBattleInputMsg>(&message.payload);
    if (input != nullptr && state_.lifecycle == BattleLifecycleState::kRunning) {
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
                *world_, state_.frame_number + 1, input->user_id, input->input_data, input->score);
            sync_replay_inputs_from_world();
        } else {
            input_seq = 1;
            while (state_.replay_inputs.size() >= input_seq) {
                ++input_seq;
            }
            state_.replay_inputs.push_back(BattleReplayInputRecord{
                .input_seq = input_seq,
                .frame_number = state_.frame_number + 1,
                .user_id = input->user_id,
                .input_data = input->input_data,
                .score = input->score,
                .trigger = {},
            });
        }
        sink_.push(BattleInputAcceptedMsg{
            .battle_id = state_.battle_id,
            .room_id = state_.room_id,
            .user_id = input->user_id,
            .input_seq = input_seq,
            .request_id = input->request_id,
            .input_data = input->input_data,
        });
        return;
    }

    const auto* tick = std::get_if<TickBattleMsg>(&message.payload);
    if (tick != nullptr && state_.lifecycle == BattleLifecycleState::kRunning) {
        if (world_ != nullptr) {
            state_.frame_number = battle_world_tick(*world_, v2::ecs::FrameContext{
                .battle_id = state_.battle_id,
                .room_id = state_.room_id,
                .frame_number = state_.frame_number + 1,
                .trigger = tick->trigger,
            });
        } else {
            ++state_.frame_number;
        }
        if (world_ != nullptr) {
            battle_world_apply_trigger_to_frame(*world_, state_.frame_number, tick->trigger);
            sync_replay_inputs_from_world();
        } else {
            for (auto& record : state_.replay_inputs) {
                if (record.frame_number == state_.frame_number) {
                    record.trigger = tick->trigger;
                }
            }
        }
        sink_.push(BattleFrameAdvancedMsg{
            .battle_id = state_.battle_id,
            .room_id = state_.room_id,
            .frame_number = state_.frame_number,
            .trigger = tick->trigger,
        });
        if (world_ != nullptr && battle_world_should_finish_for_frame_limit(*world_, state_.frame_number)) {
            finish_battle(BattleFinishReason::kFrameLimitReached, tick->trigger);
        }
        return;
    }

    const auto* end = std::get_if<EndBattleMsg>(&message.payload);
    if (end != nullptr && state_.lifecycle == BattleLifecycleState::kRunning) {
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
    if (disconnected == nullptr || state_.lifecycle != BattleLifecycleState::kRunning) {
        return;
    }

    sync_state_from_world();
    if (world_ == nullptr) {
        return;
    }
    if (!battle_world_mark_offline(*world_, disconnected->user_id)) {
        return;
    }
    sync_state_from_world();
    finish_battle(BattleFinishReason::kPlayerDisconnected, disconnected->user_id);
}

}  // namespace v2::battle
