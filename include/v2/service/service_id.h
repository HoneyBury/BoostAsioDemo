#pragma once

#include <cstdint>

namespace v2::service {

enum class ServiceId : std::uint16_t {
    kGateway = 1,
    kLogin = 2,
    kRoom = 3,
    kBattle = 4,
    kMatchmaking = 5,
    kLeaderboard = 6,
};

[[nodiscard]] constexpr const char* to_string(ServiceId id) {
    switch (id) {
        case ServiceId::kGateway:     return "gateway";
        case ServiceId::kLogin:       return "login";
        case ServiceId::kRoom:        return "room";
        case ServiceId::kBattle:      return "battle";
        case ServiceId::kMatchmaking: return "matchmaking";
        case ServiceId::kLeaderboard: return "leaderboard";
    }
    return "unknown";
}

}  // namespace v2::service
