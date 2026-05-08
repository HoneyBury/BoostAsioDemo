#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace v2::battle {

struct BattleRuntimeState {
    std::string battle_id;
    std::string room_id;
    std::vector<std::string> player_ids;
    bool started = false;
};

struct CreateBattleMsg {
    std::string battle_id;
    std::string room_id;
    std::vector<std::string> player_ids;
};

struct SubmitBattleInputMsg {
    std::string user_id;
    std::uint32_t request_id = 0;
    std::string input_data;
};

struct BattleCreatedMsg {
    std::string battle_id;
    std::string room_id;
    std::vector<std::string> player_ids;
};

struct BattleInputAcceptedMsg {
    std::string battle_id;
    std::string room_id;
    std::string user_id;
    std::uint64_t input_seq = 0;
    std::uint32_t request_id = 0;
    std::string input_data;
};

using BattleEvent = std::variant<BattleCreatedMsg, BattleInputAcceptedMsg>;

}  // namespace v2::battle
