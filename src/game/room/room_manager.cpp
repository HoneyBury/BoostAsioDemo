#include "game/room/room_manager.h"

#include <utility>

namespace game::room {

std::pair<RoomManager::CreateRoomResult, RoomManager::RoomActionOutcome> RoomManager::create_room(
    const SessionPtr& session,
    std::string room_id) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    if (key == nullptr) {
        return {CreateRoomResult::kSessionNotFound, {}};
    }

    if (room_id.empty()) {
        return {CreateRoomResult::kInvalidRoomId, {}};
    }

    if (rooms_.contains(room_id)) {
        return {CreateRoomResult::kRoomAlreadyExists, {}};
    }

    remove_from_room_unlocked(key);

    RoomState room_state;
    room_state.owner = session;
    room_state.members.emplace(key, RoomMember{.session = session, .ready = false});
    rooms_[room_id] = std::move(room_state);
    session_rooms_[key] = room_id;
    return {CreateRoomResult::kOk, RoomActionOutcome{.room_id = std::move(room_id), .player_count = 1}};
}

std::pair<RoomManager::JoinRoomResult, RoomManager::RoomActionOutcome> RoomManager::join_room(
    const SessionPtr& session,
    std::string room_id) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    if (key == nullptr) {
        return {JoinRoomResult::kSessionNotFound, {}};
    }

    if (room_id.empty()) {
        return {JoinRoomResult::kInvalidRoomId, {}};
    }

    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return {JoinRoomResult::kRoomNotFound, {}};
    }

    if (room_has_active_battle_unlocked(room_id)) {
        return {JoinRoomResult::kRoomInBattle, {.room_id = room_id, .player_count = room_it->second.members.size()}};
    }

    remove_from_room_unlocked(key);

    auto& room_state = rooms_[room_id];
    room_state.members.insert_or_assign(key, RoomMember{.session = session, .ready = false});
    session_rooms_[key] = room_id;
    return {JoinRoomResult::kOk,
            RoomActionOutcome{.room_id = std::move(room_id), .player_count = room_state.members.size()}};
}

std::pair<RoomManager::LeaveRoomResult, RoomManager::RoomActionOutcome> RoomManager::leave_room(
    const SessionPtr& session) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    if (key == nullptr) {
        return {LeaveRoomResult::kSessionNotFound, {}};
    }

    const auto room_id_it = session_rooms_.find(key);
    if (room_id_it == session_rooms_.end()) {
        return {LeaveRoomResult::kNotInRoom, {}};
    }

    const auto room_id = room_id_it->second;
    remove_from_room_unlocked(key);
    const auto room_it = rooms_.find(room_id);
    const auto player_count = room_it == rooms_.end() ? 0 : room_it->second.members.size();
    return {LeaveRoomResult::kOk,
            RoomActionOutcome{.room_id = room_id, .player_count = player_count}};
}

std::pair<RoomManager::ReadyResult, RoomManager::RoomActionOutcome> RoomManager::set_ready(const SessionPtr& session,
                                                                                            bool ready) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    if (key == nullptr) {
        return {ReadyResult::kSessionNotFound, {}};
    }

    const auto room_id_it = session_rooms_.find(key);
    if (room_id_it == session_rooms_.end()) {
        return {ReadyResult::kNotInRoom, {}};
    }

    auto room_it = rooms_.find(room_id_it->second);
    if (room_it == rooms_.end()) {
        return {ReadyResult::kNotInRoom, {}};
    }

    if (room_has_active_battle_unlocked(room_id_it->second)) {
        return {ReadyResult::kRoomInBattle, {.room_id = room_id_it->second, .player_count = room_it->second.members.size()}};
    }

    room_it->second.members[key].ready = ready;
    return {ReadyResult::kOk,
            RoomActionOutcome{.room_id = room_id_it->second, .player_count = room_it->second.members.size()}};
}

bool RoomManager::transfer_session(const SessionPtr& from_session, const SessionPtr& to_session) {
    std::scoped_lock lock(mutex_);

    const auto from_key = from_session.get();
    const auto to_key = to_session.get();

    const auto session_room_it = session_rooms_.find(from_key);
    if (session_room_it == session_rooms_.end()) {
        return false;
    }

    auto room_it = rooms_.find(session_room_it->second);
    if (room_it == rooms_.end()) {
        return false;
    }

    const auto member_it = room_it->second.members.find(from_key);
    if (member_it == room_it->second.members.end()) {
        return false;
    }

    auto member = member_it->second;
    member.session = to_session;
    room_it->second.members.erase(member_it);
    room_it->second.members.insert_or_assign(to_key, std::move(member));

    if (room_it->second.owner && room_it->second.owner.get() == from_key) {
        room_it->second.owner = to_session;
    }

    session_rooms_.erase(session_room_it);
    session_rooms_[to_key] = room_it->first;
    return true;
}

void RoomManager::remove_session(const SessionPtr& session) {
    std::scoped_lock lock(mutex_);
    remove_from_room_unlocked(session.get());
}

std::optional<std::string> RoomManager::room_id_of(const SessionPtr& session) const {
    std::scoped_lock lock(mutex_);
    const auto it = session_rooms_.find(session.get());
    if (it == session_rooms_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<RoomManager::RoomSnapshot> RoomManager::room_snapshot_of(const SessionPtr& session) const {
    std::scoped_lock lock(mutex_);
    const auto it = session_rooms_.find(session.get());
    if (it == session_rooms_.end()) {
        return std::nullopt;
    }
    return snapshot_unlocked(it->second);
}

std::optional<RoomManager::RoomSnapshot> RoomManager::room_snapshot(const std::string& room_id) const {
    std::scoped_lock lock(mutex_);
    return snapshot_unlocked(room_id);
}

std::vector<RoomManager::SessionPtr> RoomManager::room_members(const std::string& room_id) const {
    std::vector<SessionPtr> members;
    std::scoped_lock lock(mutex_);
    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return members;
    }

    members.reserve(room_it->second.members.size());
    for (const auto& [_, member] : room_it->second.members) {
        members.push_back(member.session);
    }
    return members;
}

std::size_t RoomManager::room_count() const {
    std::scoped_lock lock(mutex_);
    return rooms_.size();
}

std::size_t RoomManager::member_count(const std::string& room_id) const {
    std::scoped_lock lock(mutex_);
    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return 0;
    }
    return room_it->second.members.size();
}

void RoomManager::set_member_user_id(const SessionPtr& session, std::string user_id) {
    std::scoped_lock lock(mutex_);
    const auto key = session.get();
    if (key == nullptr) {
        return;
    }

    const auto sr = session_rooms_.find(key);
    if (sr == session_rooms_.end()) {
        return;
    }

    auto room_it = rooms_.find(sr->second);
    if (room_it == rooms_.end()) {
        return;
    }

    auto mit = room_it->second.members.find(key);
    if (mit == room_it->second.members.end()) {
        return;
    }

    mit->second.member_user_id = std::move(user_id);
}

bool RoomManager::room_has_active_battle_unlocked(const std::string& room_id) const {
    if (!battle_active_query_) {
        return false;
    }
    return battle_active_query_(room_id);
}

void RoomManager::set_battle_active_query(std::function<bool(const std::string&)> query) {
    std::scoped_lock lock(mutex_);
    battle_active_query_ = std::move(query);
}

void RoomManager::remove_from_room_unlocked(SessionKey session_key) {
    const auto session_room_it = session_rooms_.find(session_key);
    if (session_room_it == session_rooms_.end()) {
        return;
    }

    const auto room_id = session_room_it->second;
    auto room_it = rooms_.find(room_id);
    if (room_it != rooms_.end()) {
        room_it->second.members.erase(session_key);

        if (room_it->second.owner && room_it->second.owner.get() == session_key) {
            if (room_it->second.members.empty()) {
                room_it->second.owner.reset();
            } else {
                room_it->second.owner = room_it->second.members.begin()->second.session;
            }
        }

        if (room_it->second.members.empty()) {
            rooms_.erase(room_it);
        }
    }

    session_rooms_.erase(session_room_it);
}

std::optional<RoomManager::RoomSnapshot> RoomManager::snapshot_unlocked(const std::string& room_id) const {
    const auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return std::nullopt;
    }

    RoomSnapshot snapshot;
    snapshot.room_id = room_id;
    snapshot.owner = room_it->second.owner;
    snapshot.battle_started = room_has_active_battle_unlocked(room_id);
    snapshot.members.reserve(room_it->second.members.size());

    for (const auto& [_, member] : room_it->second.members) {
        snapshot.members.push_back(member);
    }
    return snapshot;
}

}  // namespace game::room
