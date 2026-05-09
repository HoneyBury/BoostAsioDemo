#include "v2/gateway/battle_protocol_codec.h"

#include <fmt/format.h>

#include <charconv>
#include <limits>
#include <system_error>

namespace v2::gateway {

namespace {

std::optional<std::map<std::string, std::string, std::less<>>> parse_key_value_segments(
    std::string_view body,
    std::string_view prefix) {
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }

    std::map<std::string, std::string, std::less<>> fields;
    auto rest = body.substr(prefix.size());
    while (!rest.empty()) {
        const auto next_delimiter = rest.find(':');
        const auto segment = rest.substr(0, next_delimiter);
        const auto separator = segment.find('=');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= segment.size()) {
            return std::nullopt;
        }

        fields.emplace(std::string(segment.substr(0, separator)), std::string(segment.substr(separator + 1)));
        if (next_delimiter == std::string_view::npos) {
            break;
        }
        rest = rest.substr(next_delimiter + 1);
    }

    return fields;
}

std::optional<std::uint64_t> parse_u64(std::string_view value) {
    std::uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::uint32_t> parse_u32(std::string_view value) {
    const auto parsed = parse_u64(value);
    if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*parsed);
}

}  // namespace

std::optional<v2::battle::BattleFinishReason> parse_battle_finish_request(std::string_view body) {
    constexpr std::string_view prefix = "finish:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }

    const auto reason = body.substr(prefix.size());
    if (reason.empty() || reason == "finished") {
        return v2::battle::BattleFinishReason::kFinished;
    }
    if (reason == "surrender") {
        return v2::battle::BattleFinishReason::kSurrender;
    }
    if (reason == "timeout") {
        return v2::battle::BattleFinishReason::kTimeout;
    }
    if (reason == "user_requested") {
        return v2::battle::BattleFinishReason::kUserRequested;
    }
    return v2::battle::BattleFinishReason::kFinished;
}

std::optional<ParsedBattleStartedBody> parse_battle_started_body(std::string_view body) {
    const auto fields = parse_key_value_segments(body, "battle_started:");
    if (!fields) {
        return std::nullopt;
    }

    const auto room_it = fields->find("room_id");
    const auto battle_it = fields->find("battle_id");
    if (room_it == fields->end() || battle_it == fields->end()) {
        return std::nullopt;
    }

    return ParsedBattleStartedBody{
        .room_id = room_it->second,
        .battle_id = battle_it->second,
    };
}

std::optional<ParsedBattleStateBody> parse_battle_state_body(std::string_view body) {
    if (!body.starts_with("battle_state:")) {
        return std::nullopt;
    }
    ParsedBattleStateBody parsed{};

    const auto rest = body.substr(std::string_view("battle_state:").size());
    const auto kind_pos = rest.find("kind=");
    const auto room_pos = rest.find(":room_id=");
    const auto battle_pos = rest.find(":battle_id=");
    if (kind_pos != 0 || room_pos == std::string_view::npos || battle_pos == std::string_view::npos) {
        return std::nullopt;
    }
    parsed.kind = std::string(rest.substr(5, room_pos - 5));

    const auto room_value_start = room_pos + std::string_view(":room_id=").size();
    parsed.room_id = std::string(rest.substr(room_value_start, battle_pos - room_value_start));

    const auto battle_value_start = battle_pos + std::string_view(":battle_id=").size();
    const auto tail = rest.substr(battle_value_start);
    const auto next_field = tail.find(':');
    if (next_field == std::string_view::npos) {
        parsed.battle_id = std::string(tail);
        return parsed;
    }

    parsed.battle_id = std::string(tail.substr(0, next_field));
    auto extra = tail.substr(next_field + 1);
    if (extra.starts_with("frame=")) {
        const auto trigger_pos = extra.find(":trigger=");
        const auto frame_value = trigger_pos == std::string_view::npos
                                     ? extra.substr(std::string_view("frame=").size())
                                     : extra.substr(std::string_view("frame=").size(),
                                                    trigger_pos - std::string_view("frame=").size());
        parsed.frame = parse_u32(frame_value);
        if (!parsed.frame) {
            return std::nullopt;
        }
        if (trigger_pos != std::string_view::npos) {
            parsed.trigger = std::string(extra.substr(trigger_pos + std::string_view(":trigger=").size()));
        }
        return parsed;
    }

    if (extra.starts_with("reason=")) {
        const auto user_pos = extra.find(":user_id=");
        const auto reason_value = user_pos == std::string_view::npos
                                      ? extra.substr(std::string_view("reason=").size())
                                      : extra.substr(std::string_view("reason=").size(),
                                                     user_pos - std::string_view("reason=").size());
        parsed.reason = std::string(reason_value);
        if (user_pos != std::string_view::npos) {
            parsed.user_id = std::string(extra.substr(user_pos + std::string_view(":user_id=").size()));
        }
        return parsed;
    }

    return parsed;
}

std::optional<ParsedBattleInputResponseBody> parse_battle_input_response_body(std::string_view body) {
    const auto fields = parse_key_value_segments(body, "input_seq:");
    if (!fields) {
        return std::nullopt;
    }

    const auto seq_it = fields->find("seq");
    if (seq_it == fields->end()) {
        return std::nullopt;
    }
    const auto seq = parse_u64(seq_it->second);
    if (!seq) {
        return std::nullopt;
    }
    return ParsedBattleInputResponseBody{.input_seq = *seq};
}

std::optional<ParsedBattleInputPushBody> parse_battle_input_push_body(std::string_view body) {
    constexpr std::string_view prefix = "battle_input:user_id=";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto rest = body.substr(prefix.size());
    const auto seq_marker = rest.find(":seq=");
    if (seq_marker == std::string_view::npos) {
        return std::nullopt;
    }
    const auto input_marker = rest.find(":input=", seq_marker + std::string_view(":seq=").size());
    if (input_marker == std::string_view::npos) {
        return std::nullopt;
    }

    const auto user_id = rest.substr(0, seq_marker);
    const auto seq_text = rest.substr(seq_marker + std::string_view(":seq=").size(),
                                      input_marker - (seq_marker + std::string_view(":seq=").size()));
    const auto seq = parse_u64(seq_text);
    if (!seq) {
        return std::nullopt;
    }
    const auto input = rest.substr(input_marker + std::string_view(":input=").size());

    return ParsedBattleInputPushBody{
        .user_id = std::string(user_id),
        .input_seq = *seq,
        .input_data = std::string(input),
    };
}

std::string format_battle_started_body(std::string_view room_id, std::string_view battle_id) {
    return fmt::format("battle_started:room_id={}:battle_id={}", room_id, battle_id);
}

std::string format_battle_state_body(std::string_view room_id, std::string_view battle_id) {
    return fmt::format("battle_state:kind=started:room_id={}:battle_id={}", room_id, battle_id);
}

std::string format_battle_input_response_body(std::uint64_t input_seq) {
    return fmt::format("input_seq:seq={}", input_seq);
}

std::string format_battle_input_push_body(std::string_view user_id,
                                          std::uint64_t input_seq,
                                          std::string_view input_data) {
    return fmt::format("battle_input:user_id={}:seq={}:input={}", user_id, input_seq, input_data);
}

std::string format_battle_end_accepted_body(v2::battle::BattleFinishReason reason) {
    return fmt::format("battle_end_accepted:{}", v2::battle::to_string(reason));
}

std::string format_battle_frame_body(const v2::battle::BattleFrameAdvancedMsg& frame) {
    return fmt::format("battle_state:kind=frame:room_id={}:battle_id={}:frame={}:trigger={}",
                       frame.room_id,
                       frame.battle_id,
                       frame.frame_number,
                       frame.trigger);
}

std::string format_battle_settlement_body(const v2::battle::BattleSettlementPreparedMsg& settlement) {
    return fmt::format("battle_state:kind=settlement:room_id={}:battle_id={}:reason={}:user_id={}",
                       settlement.room_id,
                       settlement.battle_id,
                       v2::battle::to_string(settlement.reason),
                       settlement.triggering_user_id);
}

std::string format_battle_finished_body(const v2::battle::BattleFinishedMsg& finished) {
    return fmt::format("battle_state:kind=finished:room_id={}:battle_id={}:reason={}:user_id={}",
                       finished.room_id,
                       finished.battle_id,
                       v2::battle::to_string(finished.reason),
                       finished.triggering_user_id);
}

}  // namespace v2::gateway
