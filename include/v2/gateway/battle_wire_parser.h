#pragma once

#include "v2/battle/message_types.h"
#include "v2/gateway/battle_protocol_codec.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace v2::gateway {

struct ParsedBattleEndAcceptedBody {
    v2::battle::BattleFinishReason reason = v2::battle::BattleFinishReason::kFinished;
};

using BattleWireBody = std::variant<ParsedBattleStartedBody,
                                    ParsedBattleStateBody,
                                    ParsedBattleInputResponseBody,
                                    ParsedBattleInputPushBody,
                                    ParsedBattleEndAcceptedBody,
                                    v2::battle::BattleFinishReason>;

enum class BattleWireBodyKind : std::uint8_t {
    kFinishRequest = 0,
    kStarted = 1,
    kState = 2,
    kInputResponse = 3,
    kInputPush = 4,
    kEndAccepted = 5,
};

[[nodiscard]] std::optional<ParsedBattleEndAcceptedBody> parse_battle_end_accepted_body(std::string_view body);
[[nodiscard]] std::optional<BattleWireBody> parse_battle_wire_body(std::string_view body);
[[nodiscard]] BattleWireBodyKind battle_wire_body_kind(const BattleWireBody& body) noexcept;
[[nodiscard]] bool validate_battle_wire_body(const BattleWireBody& body) noexcept;

}  // namespace v2::gateway
