#pragma once

#include "net/session.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::room {

class RoomManager {
public:
    using SessionPtr = std::shared_ptr<net::Session>;

    struct RoomMember {
        SessionPtr session;
        bool ready = false;
    };

    struct RoomSnapshot {
        std::string room_id;
        SessionPtr owner;
        bool battle_started = false;
        std::vector<RoomMember> members;
    };

    enum class CreateRoomResult {
        kOk,
        kSessionNotFound,
        kInvalidRoomId,
        kRoomAlreadyExists,
    };

    enum class JoinRoomResult {
        kOk,
        kSessionNotFound,
        kInvalidRoomId,
        kRoomNotFound,
        kRoomInBattle,
    };

    enum class LeaveRoomResult {
        kOk,
        kSessionNotFound,
        kNotInRoom,
    };

    enum class ReadyResult {
        kOk,
        kSessionNotFound,
        kNotInRoom,
        kRoomInBattle,
    };

    struct RoomActionOutcome {
        std::string room_id;
        std::size_t player_count = 0;
    };

    std::pair<CreateRoomResult, RoomActionOutcome> create_room(const SessionPtr& session, std::string room_id);
    std::pair<JoinRoomResult, RoomActionOutcome> join_room(const SessionPtr& session, std::string room_id);
    std::pair<LeaveRoomResult, RoomActionOutcome> leave_room(const SessionPtr& session);
    std::pair<ReadyResult, RoomActionOutcome> set_ready(const SessionPtr& session, bool ready);
    bool transfer_session(const SessionPtr& from_session, const SessionPtr& to_session);

    void remove_session(const SessionPtr& session);

    [[nodiscard]] std::optional<std::string> room_id_of(const SessionPtr& session) const;
    [[nodiscard]] std::optional<RoomSnapshot> room_snapshot_of(const SessionPtr& session) const;
    [[nodiscard]] std::optional<RoomSnapshot> room_snapshot(const std::string& room_id) const;
    [[nodiscard]] std::vector<SessionPtr> room_members(const std::string& room_id) const;
    [[nodiscard]] std::size_t room_count() const;
    [[nodiscard]] std::size_t member_count(const std::string& room_id) const;

    /// 将「房间是否已在战斗中」与 `BattleManager` 对齐（单一事实源，v1.1.4 / T06）。
    /// 凡同时装配 `BattleService` / `BattleManager` 的网关装配点必须在启动前调用，
    /// 典型：`room.set_battle_active_query([&battle](auto& id) { return battle.battle_started(id); });`
    /// 若不设置，则房间内 `battle_started` 视图视为恒为假（仅适用于纯演示 / 不包含战斗的子集）。
    void set_battle_active_query(std::function<bool(const std::string&)> query);

    // COW snapshot: snapshot member list under lock, then invoke callback outside lock.
    template <typename F>
    void broadcast_to_room(const std::string& room_id, F&& callback) const {
        std::vector<SessionPtr> members;
        {
            std::scoped_lock lock(mutex_);
            const auto it = rooms_.find(room_id);
            if (it == rooms_.end()) return;
            members.reserve(it->second.members.size());
            for (const auto& [key, member] : it->second.members) {
                members.push_back(member.session);
            }
        }
        for (const auto& session : members) {
            callback(session);
        }
    }

private:
    using SessionKey = const net::Session*;

    struct RoomState {
        SessionPtr owner;
        std::unordered_map<SessionKey, RoomMember> members;
    };

    void remove_from_room_unlocked(SessionKey session_key);
    [[nodiscard]] bool room_has_active_battle_unlocked(const std::string& room_id) const;
    [[nodiscard]] std::optional<RoomSnapshot> snapshot_unlocked(const std::string& room_id) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RoomState> rooms_;
    std::unordered_map<SessionKey, std::string> session_rooms_;
    std::function<bool(const std::string&)> battle_active_query_;
};

}  // namespace game::room
