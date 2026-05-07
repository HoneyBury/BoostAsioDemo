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
        /// 由 `RoomService` 在进入房间链路写入的稳定身份缓存（v1.1.8 / T09）。
        /// 非空则 `RoomService` 组播 `room_state` 与开战收集 `player_ids` 优先用此字段，减少对 `SessionManager` 的回查；
        /// 顶号 `transfer_session` 时随成员记录迁移；空则退回 `login_context_of`。
        std::string member_user_id;
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
    /// **仅用于**顶号恢复：把 `from_session` 的房间席位（含 `member_user_id` / ready）迁到 `to_session`。
    /// 战斗中仍允许迁移：战斗参与者以 `user_id` 为主键，与 `Session` 句柄解耦（v1.1.8 / T09）。
    bool transfer_session(const SessionPtr& from_session, const SessionPtr& to_session);

    void remove_session(const SessionPtr& session);

    [[nodiscard]] std::optional<std::string> room_id_of(const SessionPtr& session) const;
    [[nodiscard]] std::optional<RoomSnapshot> room_snapshot_of(const SessionPtr& session) const;
    [[nodiscard]] std::optional<RoomSnapshot> room_snapshot(const std::string& room_id) const;
    [[nodiscard]] std::vector<SessionPtr> room_members(const std::string& room_id) const;
    [[nodiscard]] std::size_t room_count() const;
    [[nodiscard]] std::size_t member_count(const std::string& room_id) const;

    /// 回填当前成员的 `member_user_id`（需会话已在房间内）。供 `RoomService` 在进入房间后主链上使用。
    void set_member_user_id(const SessionPtr& session, std::string user_id);

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
