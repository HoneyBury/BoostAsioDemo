#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace v2::gateway {

struct ParsedLoginResponseBody {
    std::string user_id;
};

struct ParsedRoomResponseBody {
    std::string room_id;
};

struct ParsedSessionKickedBody {
    std::string reason;
    bool room_transferred = false;
};

struct ParsedSessionResumedBody {
    std::string room_id;
    bool in_battle = false;
};

[[nodiscard]] std::optional<ParsedLoginResponseBody> parse_login_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomResponseBody> parse_room_create_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomResponseBody> parse_room_join_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedSessionKickedBody> parse_session_kicked_body(std::string_view body);
[[nodiscard]] std::optional<ParsedSessionResumedBody> parse_session_resumed_body(std::string_view body);

}  // namespace v2::gateway
