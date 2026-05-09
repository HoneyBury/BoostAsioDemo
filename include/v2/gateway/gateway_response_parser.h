#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

struct ParsedRoomStatePushBody {
    std::string room_id;
    std::string owner_user_id;
    std::vector<std::string> member_ids;
    std::vector<std::string> ready_ids;
    bool in_battle = false;
};

struct ParsedErrorResponseBody {
    std::string reason;
};

using GatewayResponseBody = std::variant<ParsedLoginResponseBody,
                                         ParsedRoomResponseBody,
                                         ParsedSessionKickedBody,
                                         ParsedSessionResumedBody,
                                         ParsedRoomStatePushBody,
                                         ParsedErrorResponseBody>;

[[nodiscard]] std::optional<ParsedLoginResponseBody> parse_login_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomResponseBody> parse_room_create_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomResponseBody> parse_room_join_response_body(std::string_view body);
[[nodiscard]] std::optional<ParsedSessionKickedBody> parse_session_kicked_body(std::string_view body);
[[nodiscard]] std::optional<ParsedSessionResumedBody> parse_session_resumed_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomStatePushBody> parse_room_state_push_body(std::string_view body);
[[nodiscard]] std::optional<ParsedRoomStatePushBody> parse_room_state_push_body_alt(std::string_view body);
[[nodiscard]] std::optional<ParsedErrorResponseBody> parse_error_response_body(std::string_view body);

}  // namespace v2::gateway
