#pragma once

#include "v2/battle/message_types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace v2::gateway {

struct ParsedBattleStartedBody {
    std::string room_id;
    std::string battle_id;
};

struct ParsedBattleStateBody {
    std::string kind;
    std::string room_id;
    std::string battle_id;
    std::optional<std::uint32_t> frame;
    std::optional<std::string> trigger;
    std::optional<std::string> reason;
    std::optional<std::string> user_id;
};

struct ParsedBattleInputResponseBody {
    std::uint64_t input_seq = 0;
};

struct ParsedBattleInputPushBody {
    std::string user_id;
    std::uint64_t input_seq = 0;
    std::string input_data;
};

[[nodiscard]] std::optional<v2::battle::BattleFinishReason> parse_battle_finish_request(std::string_view body);
[[nodiscard]] std::optional<ParsedBattleStartedBody> parse_battle_started_body(std::string_view body);
[[nodiscard]] std::optional<ParsedBattleStateBody> parse_battle_state_body(std::string_view body);
[[nodiscard]] std::optional<ParsedBattleInputResponseBody> parse_battle_input_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedBattleInputPushBody> parse_battle_input_push_body(std::string_view body);
[[nodiscard]] std::string format_battle_started_body(std::string_view room_id, std::string_view battle_id);
[[nodiscard]] std::string format_battle_state_body(std::string_view room_id, std::string_view battle_id);
[[nodiscard]] std::string format_battle_input_response_body(std::uint64_t input_seq);
[[nodiscard]] std::string format_battle_input_push_body(std::string_view user_id,
                                                        std::uint64_t input_seq,
                                                        std::string_view input_data);
[[nodiscard]] std::string format_battle_end_accepted_body(v2::battle::BattleFinishReason reason);
[[nodiscard]] std::string format_battle_frame_body(const v2::battle::BattleFrameAdvancedMsg& frame);
[[nodiscard]] std::string format_battle_settlement_body(const v2::battle::BattleSettlementPreparedMsg& settlement);
[[nodiscard]] std::string format_battle_finished_body(const v2::battle::BattleFinishedMsg& finished);

}  // namespace v2::gateway
