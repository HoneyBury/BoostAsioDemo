#include "v2/gateway/gateway_response_parser.h"

#include <sstream>
#include <vector>

namespace v2::gateway {

namespace {

std::vector<std::string> split(std::string_view body, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream{std::string(body)};
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

}  // namespace

std::optional<ParsedLoginResponseBody> parse_login_response_body(std::string_view body) {
    constexpr std::string_view prefix = "login_ok:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto remainder = std::string_view(body).substr(prefix.size());
    const auto parts = split(remainder, ':');
    if (parts.empty() || parts.front().empty()) {
        return std::nullopt;
    }
    return ParsedLoginResponseBody{.user_id = parts.front()};
}

std::optional<ParsedRoomResponseBody> parse_room_create_response_body(std::string_view body) {
    constexpr std::string_view prefix = "room_created:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto room_id = std::string(std::string_view(body).substr(prefix.size()));
    if (room_id.empty()) {
        return std::nullopt;
    }
    return ParsedRoomResponseBody{.room_id = room_id};
}

std::optional<ParsedRoomResponseBody> parse_room_join_response_body(std::string_view body) {
    constexpr std::string_view prefix = "room_joined:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto remainder = std::string_view(body).substr(prefix.size());
    const auto parts = split(remainder, ':');
    if (parts.empty() || parts.front().empty()) {
        return std::nullopt;
    }
    return ParsedRoomResponseBody{.room_id = parts.front()};
}

std::optional<ParsedSessionKickedBody> parse_session_kicked_body(std::string_view body) {
    constexpr std::string_view prefix = "session_kicked:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto remainder = std::string_view(body).substr(prefix.size());
    const auto parts = split(remainder, ':');
    if (parts.empty() || parts.front().empty()) {
        return std::nullopt;
    }
    return ParsedSessionKickedBody{
        .reason = parts.front(),
        .room_transferred = parts.size() >= 2 && parts[1] == "room_transferred",
    };
}

std::optional<ParsedSessionResumedBody> parse_session_resumed_body(std::string_view body) {
    constexpr std::string_view prefix = "session_resumed:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto remainder = std::string_view(body).substr(prefix.size());
    const auto parts = split(remainder, ':');
    if (parts.size() < 2 || parts[0].empty()) {
        return std::nullopt;
    }
    ParsedSessionResumedBody parsed{
        .room_id = parts[0],
        .in_battle = false,
    };
    if (parts[1].starts_with("battle=")) {
        parsed.in_battle = parts[1].substr(7) == "1";
    }
    return parsed;
}

std::optional<ParsedRoomStatePushBody> parse_room_state_push_body(std::string_view body) {
    constexpr std::string_view prefix = "room_state:";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto remainder = std::string_view(body).substr(prefix.size());
    const auto parts = split(remainder, ':');
    if (parts.size() < 2 || parts[0].empty()) {
        return std::nullopt;
    }

    ParsedRoomStatePushBody parsed{
        .room_id = parts[0],
        .owner_user_id = parts[1],
    };

    if (parts.size() >= 3 && !parts[2].empty()) {
        parsed.member_ids = split(parts[2], ',');
    }
    if (parts.size() >= 4 && !parts[3].empty()) {
        parsed.ready_ids = split(parts[3], ',');
    }
    if (parts.size() >= 5 && parts[4] == "in_battle") {
        parsed.in_battle = true;
    }

    return parsed;
}

std::optional<ParsedRoomStatePushBody> parse_room_state_push_body_alt(std::string_view body) {
    constexpr std::string_view prefix = "room_state:room_id=";
    if (!body.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto rest = std::string_view(body).substr(prefix.size());
    if (rest.empty()) {
        return std::nullopt;
    }
    const auto owner_pos = rest.find(":owner_id=");
    if (owner_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto room_id_text = rest.substr(0, owner_pos);
    if (room_id_text.empty()) {
        return std::nullopt;
    }

    ParsedRoomStatePushBody parsed{
        .room_id = std::string(room_id_text),
    };

    const auto members_pos = rest.find(":members=", owner_pos);
    const auto owner_value_start = owner_pos + std::string_view(":owner_id=").size();
    if (members_pos == std::string_view::npos) {
        parsed.owner_user_id = std::string(rest.substr(owner_value_start));
        return parsed;
    }
    parsed.owner_user_id = std::string(rest.substr(owner_value_start, members_pos - owner_value_start));

    const auto ready_pos = rest.find(":ready=", members_pos);
    const auto members_value_start = members_pos + std::string_view(":members=").size();
    if (ready_pos == std::string_view::npos) {
        parsed.member_ids = split(rest.substr(members_value_start), ',');
        return parsed;
    }

    if (members_value_start < ready_pos) {
        parsed.member_ids = split(rest.substr(members_value_start, ready_pos - members_value_start), ',');
    }

    const auto battle_pos = rest.find(":in_battle=", ready_pos);
    const auto ready_value_start = ready_pos + std::string_view(":ready=").size();
    if (battle_pos == std::string_view::npos) {
        parsed.ready_ids = split(rest.substr(ready_value_start), ',');
        return parsed;
    }

    if (ready_value_start < battle_pos) {
        parsed.ready_ids = split(rest.substr(ready_value_start, battle_pos - ready_value_start), ',');
    }

    const auto battle_value = rest.substr(battle_pos + std::string_view(":in_battle=").size());
    parsed.in_battle = (battle_value == "1" || battle_value == "true");

    return parsed;
}

std::optional<ParsedErrorResponseBody> parse_error_response_body(std::string_view body) {
    if (body.empty()) {
        return std::nullopt;
    }
    return ParsedErrorResponseBody{.reason = std::string(body)};
}

}  // namespace v2::gateway
