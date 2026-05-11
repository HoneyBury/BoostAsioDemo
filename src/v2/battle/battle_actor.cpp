#include "v2/battle/battle_actor.h"

#include <utility>

#include "v2/battle/battle_snapshot.h"

namespace v2::battle {

BattleRuntimeState BattleActor::runtime_state() const {
    if (world_ == nullptr) {
        return {};
    }
    return battle_world_runtime_state(*world_);
}

BattleRuntimeState BattleActor::state() const {
    return runtime_state();
}

void BattleActor::finish_battle(BattleFinishReason reason, std::string triggering_user_id) {
    const auto current_state = runtime_state();
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
            *world_, finished_state.battle_id, finished_state.room_id, finished_state.participants, reason, finished_state.frame_number);
    } else {
        result = BattleResultSummary{
            .battle_id = finished_state.battle_id,
            .room_id = finished_state.room_id,
            .reason = reason,
            .winner_user_id = std::nullopt,
            .scores = {},
            .total_frames = finished_state.frame_number,
        };
    }

    sink_.push(BattleSettlementPreparedMsg{
        .battle_id = finished_state.battle_id,
        .room_id = finished_state.room_id,
        .reason = reason,
        .triggering_user_id = triggering_user_id,
        .total_frames = finished_state.frame_number,
        .participant_user_ids = std::move(participant_user_ids),
        .replay_inputs = finished_state.replay_inputs,
        .result = std::move(result),
    });
    sink_.push(BattleFinishedMsg{
        .battle_id = finished_state.battle_id,
        .room_id = finished_state.room_id,
        .reason = reason,
        .triggering_user_id = std::move(triggering_user_id),
    });
}

std::string BattleActor::take_snapshot() const {
    if (world_ == nullptr) {
        return {};
    }
    return battle_world_snapshot_to_json(*world_);
}

bool BattleActor::restore_from_snapshot(const std::string& snapshot_data) {
    if (world_ == nullptr) {
        return false;
    }
    return battle_world_restore_from_json(*world_, snapshot_data);
}

void BattleActor::on_message(v2::actor::Message&& message) {
    // ── Create ────────────────────────────────────────────────
    if (const auto* create = std::get_if<CreateBattleMsg>(&message.payload)) {
        world_ = create_battle_world(create->battle_id, create->room_id, create->player_ids, create->max_frames);

        sink_.push(BattleCreatedMsg{
            .battle_id = create->battle_id,
            .room_id = create->room_id,
            .player_ids = create->player_ids,
        });
        return;
    }

    // ── Input ─────────────────────────────────────────────────
    const auto* input = std::get_if<SubmitBattleInputMsg>(&message.payload);
    if (input != nullptr) {
        if (world_ == nullptr) {
            return;
        }
        const auto result = battle_world_process_input(
            *world_, input->user_id, input->input_data, input->score, input->submitted_frame);
        if (!result.accepted) {
            return;
        }
        sink_.push(BattleInputAcceptedMsg{
            .battle_id = runtime_state().battle_id,
            .room_id = runtime_state().room_id,
            .user_id = input->user_id,
            .input_seq = result.input_seq,
            .request_id = input->request_id,
            .input_data = input->input_data,
        });
        return;
    }

    // ── Tick ──────────────────────────────────────────────────
    const auto* tick = std::get_if<TickBattleMsg>(&message.payload);
    if (tick != nullptr) {
        if (world_ == nullptr) {
            return;
        }
        const auto frame_result = battle_world_advance_frame(
            *world_, runtime_state().frame_number + 1, tick->trigger);
        sink_.push(BattleFrameAdvancedMsg{
            .battle_id = runtime_state().battle_id,
            .room_id = runtime_state().room_id,
            .frame_number = frame_result.frame_number,
            .trigger = tick->trigger,
        });
        if (frame_result.should_finish) {
            finish_battle(frame_result.finish_reason, tick->trigger);
        }
        return;
    }

    // ── End ───────────────────────────────────────────────────
    const auto* end = std::get_if<EndBattleMsg>(&message.payload);
    if (end != nullptr && runtime_state().lifecycle == BattleLifecycleState::kRunning) {
        finish_battle(end->reason, end->triggering_user_id);
        return;
    }

    // ── Ack ───────────────────────────────────────────────────
    const auto* ack = std::get_if<FrameAckMsg>(&message.payload);
    if (ack != nullptr) {
        if (world_ != nullptr) {
            battle_world_record_frame_ack(*world_, ack->user_id, ack->frame_number);
        }
        sink_.push(*ack);
        return;
    }

    // ── Disconnect ────────────────────────────────────────────
    const auto* disconnected = std::get_if<PlayerDisconnectedMsg>(&message.payload);
    if (disconnected != nullptr) {
        if (world_ == nullptr) {
            return;
        }
        const auto result = battle_world_handle_disconnect(*world_, disconnected->user_id);
        if (result.battle_should_finish) {
            finish_battle(BattleFinishReason::kPlayerDisconnected, disconnected->user_id);
        }
        return;
    }
}

}  // namespace v2::battle
