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

}  // namespace v2::gateway
