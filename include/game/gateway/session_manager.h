#pragma once

#include "net/session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace game::gateway {

class SessionManager {
public:
    using SessionPtr = std::shared_ptr<net::Session>;

    enum class JoinRoomResult {
        kOk,
        kSessionNotFound,
        kNotAuthenticated,
        kInvalidRoomId,
        kBattleAlreadyStarted,
    };

    enum class StartBattleResult {
        kOk,
        kSessionNotFound,
        kNotAuthenticated,
        kNotInRoom,
        kAlreadyStarted,
        kNotEnoughPlayers,
    };

    struct JoinRoomOutcome {
        JoinRoomResult result = JoinRoomResult::kSessionNotFound;
        std::string room_id;
        std::size_t player_count = 0;
    };

    struct StartBattleOutcome {
        StartBattleResult result = StartBattleResult::kSessionNotFound;
        std::string room_id;
        std::size_t player_count = 0;
    };

    struct Snapshot {
        std::size_t active_sessions = 0;
        std::size_t authenticated_sessions = 0;
        std::size_t active_rooms = 0;
        std::size_t active_battles = 0;
    };

    std::uint64_t add_session(const SessionPtr& session);
    void remove_session(const SessionPtr& session);

    [[nodiscard]] std::vector<SessionPtr> all_sessions() const;
    [[nodiscard]] bool is_authenticated(const SessionPtr& session) const;
    [[nodiscard]] std::optional<std::string> user_id_of(const SessionPtr& session) const;
    [[nodiscard]] std::optional<std::string> room_id_of(const SessionPtr& session) const;
    [[nodiscard]] Snapshot snapshot() const;

    SessionPtr authenticate(const SessionPtr& session, std::string user_id);
    JoinRoomOutcome join_room(const SessionPtr& session, std::string room_id);
    StartBattleOutcome start_battle(const SessionPtr& session);

private:
    using SessionKey = const net::Session*;

    struct SessionRecord {
        std::uint64_t session_id = 0;
        SessionPtr session;
        std::string user_id;
        std::string room_id;
        bool authenticated = false;
        bool in_battle = false;
    };

    struct RoomRecord {
        std::unordered_set<SessionKey> members;
        bool battle_started = false;
    };

    void remove_from_room_unlocked(SessionKey session_key, SessionRecord& record);

    mutable std::mutex mutex_;
    std::uint64_t next_session_id_ = 1;
    std::unordered_map<SessionKey, SessionRecord> sessions_;
    std::unordered_map<std::string, SessionKey> user_index_;
    std::unordered_map<std::string, RoomRecord> rooms_;
};

}  // namespace game::gateway
