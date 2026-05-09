#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace v2::gateway {

struct ParsedLoginCommandBody {
    std::string user_id;
    std::string token;
    std::optional<std::string> display_name;
};

[[nodiscard]] std::optional<ParsedLoginCommandBody> parse_login_command_body(std::string_view body);
[[nodiscard]] bool validate_login_command_body(const ParsedLoginCommandBody& body) noexcept;

[[nodiscard]] std::optional<std::string> parse_room_id_body(std::string_view body);
[[nodiscard]] bool validate_room_id_body(std::string_view body) noexcept;

[[nodiscard]] std::optional<bool> parse_room_ready_body(std::string_view body) noexcept;

}  // namespace v2::gateway
