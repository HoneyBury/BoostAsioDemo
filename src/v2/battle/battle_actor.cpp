#include "v2/battle/battle_actor.h"

#include <utility>

namespace v2::battle {

void BattleActor::on_message(v2::actor::Message&& message) {
    if (const auto* create = std::get_if<CreateBattleMsg>(&message.payload)) {
        state_.battle_id = create->battle_id;
        state_.room_id = create->room_id;
        state_.player_ids = create->player_ids;
        state_.started = true;

        sink_.push(BattleCreatedMsg{
            .battle_id = state_.battle_id,
            .room_id = state_.room_id,
            .player_ids = state_.player_ids,
        });
        return;
    }

    const auto* input = std::get_if<SubmitBattleInputMsg>(&message.payload);
    if (input == nullptr || !state_.started) {
        return;
    }

    sink_.push(BattleInputAcceptedMsg{
        .battle_id = state_.battle_id,
        .room_id = state_.room_id,
        .user_id = input->user_id,
        .input_seq = next_input_seq_++,
        .request_id = input->request_id,
        .input_data = input->input_data,
    });
}

}  // namespace v2::battle
