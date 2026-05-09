#include "v2/gateway/battle_wire_parser.h"

namespace v2::gateway {

std::optional<ParsedBattleEndAcceptedBody> parse_battle_end_accepted_body(std::string_view body) {
    constexpr std::string_view prefix = "battle_end_accepted:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }

    const auto reason_text = body.substr(prefix.size());
    v2::battle::BattleFinishReason reason = v2::battle::BattleFinishReason::kFinished;
    if (reason_text == "surrender") {
        reason = v2::battle::BattleFinishReason::kSurrender;
    } else if (reason_text == "timeout") {
        reason = v2::battle::BattleFinishReason::kTimeout;
    } else if (reason_text == "frame_limit_reached") {
        reason = v2::battle::BattleFinishReason::kFrameLimitReached;
    } else if (reason_text == "player_disconnected") {
        reason = v2::battle::BattleFinishReason::kPlayerDisconnected;
    }
    return ParsedBattleEndAcceptedBody{.reason = reason};
}

std::optional<BattleWireBody> parse_battle_wire_body(std::string_view body) {
    if (const auto finish_request = parse_battle_finish_request(body)) {
        return BattleWireBody{*finish_request};
    }
    if (const auto started = parse_battle_started_body(body)) {
        return BattleWireBody{*started};
    }
    if (const auto state = parse_battle_state_body(body)) {
        return BattleWireBody{*state};
    }
    if (const auto input_response = parse_battle_input_response_body(body)) {
        return BattleWireBody{*input_response};
    }
    if (const auto input_push = parse_battle_input_push_body(body)) {
        return BattleWireBody{*input_push};
    }
    if (const auto end_accepted = parse_battle_end_accepted_body(body)) {
        return BattleWireBody{*end_accepted};
    }
    return std::nullopt;
}

BattleWireBodyKind battle_wire_body_kind(const BattleWireBody& body) noexcept {
    if (std::holds_alternative<v2::battle::BattleFinishReason>(body)) {
        return BattleWireBodyKind::kFinishRequest;
    }
    if (std::holds_alternative<ParsedBattleStartedBody>(body)) {
        return BattleWireBodyKind::kStarted;
    }
    if (std::holds_alternative<ParsedBattleStateBody>(body)) {
        return BattleWireBodyKind::kState;
    }
    if (std::holds_alternative<ParsedBattleInputResponseBody>(body)) {
        return BattleWireBodyKind::kInputResponse;
    }
    if (std::holds_alternative<ParsedBattleInputPushBody>(body)) {
        return BattleWireBodyKind::kInputPush;
    }
    return BattleWireBodyKind::kEndAccepted;
}

bool validate_battle_wire_body(const BattleWireBody& body) noexcept {
    if (const auto* started = std::get_if<ParsedBattleStartedBody>(&body)) {
        return !started->room_id.empty() && !started->battle_id.empty();
    }
    if (const auto* state = std::get_if<ParsedBattleStateBody>(&body)) {
        if (state->kind.empty() || state->room_id.empty() || state->battle_id.empty()) {
            return false;
        }
        if (state->kind == "started") {
            return !state->frame.has_value() && !state->reason.has_value();
        }
        if (state->kind == "frame") {
            return state->frame.has_value() && state->trigger.has_value();
        }
        if (state->kind == "finished" || state->kind == "settlement") {
            return state->reason.has_value() && state->user_id.has_value();
        }
        return false;
    }
    if (const auto* input_response = std::get_if<ParsedBattleInputResponseBody>(&body)) {
        return input_response->input_seq > 0;
    }
    if (const auto* input_push = std::get_if<ParsedBattleInputPushBody>(&body)) {
        return !input_push->user_id.empty() && input_push->input_seq > 0 && !input_push->input_data.empty();
    }
    if (const auto* end_accepted = std::get_if<ParsedBattleEndAcceptedBody>(&body)) {
        return !std::string(v2::battle::to_string(end_accepted->reason)).empty();
    }
    return true;
}

}  // namespace v2::gateway
