#include "game/gateway/session_manager.h"

#include <utility>

namespace game::gateway {

std::uint64_t SessionManager::add_session(const SessionPtr& session) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    const auto session_id = next_session_id_++;
    sessions_[key] = SessionRecord{
        .session_id = session_id,
        .session = session,
    };
    return session_id;
}

void SessionManager::remove_session(const SessionPtr& session) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    const auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return;
    }

    auto& record = it->second;
    if (!record.user_id.empty()) {
        const auto user_it = user_index_.find(record.user_id);
        if (user_it != user_index_.end() && user_it->second == key) {
            user_index_.erase(user_it);
        }
    }

    remove_from_room_unlocked(key, record);
    sessions_.erase(it);
}

std::vector<SessionManager::SessionPtr> SessionManager::all_sessions() const {
    std::vector<SessionPtr> sessions;
    std::scoped_lock lock(mutex_);
    sessions.reserve(sessions_.size());
    for (const auto& [_, record] : sessions_) {
        sessions.push_back(record.session);
    }
    return sessions;
}

bool SessionManager::is_authenticated(const SessionPtr& session) const {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session.get());
    return it != sessions_.end() && it->second.authenticated;
}

std::optional<std::string> SessionManager::user_id_of(const SessionPtr& session) const {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session.get());
    if (it == sessions_.end() || !it->second.authenticated) {
        return std::nullopt;
    }
    return it->second.user_id;
}

std::optional<std::string> SessionManager::room_id_of(const SessionPtr& session) const {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session.get());
    if (it == sessions_.end() || it->second.room_id.empty()) {
        return std::nullopt;
    }
    return it->second.room_id;
}

SessionManager::Snapshot SessionManager::snapshot() const {
    std::scoped_lock lock(mutex_);

    Snapshot snapshot;
    snapshot.active_sessions = sessions_.size();
    snapshot.active_rooms = rooms_.size();

    for (const auto& [_, record] : sessions_) {
        if (record.authenticated) {
            ++snapshot.authenticated_sessions;
        }
    }

    for (const auto& [_, room] : rooms_) {
        if (room.battle_started) {
            ++snapshot.active_battles;
        }
    }

    return snapshot;
}

SessionManager::SessionPtr SessionManager::authenticate(const SessionPtr& session, std::string user_id) {
    SessionPtr replaced_session;

    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    const auto it = sessions_.find(key);
    if (it == sessions_.end() || user_id.empty()) {
        return nullptr;
    }

    auto& record = it->second;

    if (!record.user_id.empty()) {
        const auto user_it = user_index_.find(record.user_id);
        if (user_it != user_index_.end() && user_it->second == key) {
            user_index_.erase(user_it);
        }
    }

    const auto existing_user_it = user_index_.find(user_id);
    if (existing_user_it != user_index_.end() && existing_user_it->second != key) {
        const auto old_session_it = sessions_.find(existing_user_it->second);
        if (old_session_it != sessions_.end()) {
            old_session_it->second.authenticated = false;
            old_session_it->second.user_id.clear();
            remove_from_room_unlocked(existing_user_it->second, old_session_it->second);
            replaced_session = old_session_it->second.session;
        }
    }

    record.authenticated = true;
    record.user_id = std::move(user_id);
    user_index_[record.user_id] = key;
    return replaced_session;
}

SessionManager::JoinRoomOutcome SessionManager::join_room(const SessionPtr& session, std::string room_id) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    const auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return {};
    }

    auto& record = it->second;
    if (!record.authenticated) {
        return {JoinRoomResult::kNotAuthenticated, "", 0};
    }

    if (room_id.empty()) {
        return {JoinRoomResult::kInvalidRoomId, "", 0};
    }

    auto room_it = rooms_.find(room_id);
    if (room_it != rooms_.end() && room_it->second.battle_started) {
        return {JoinRoomResult::kBattleAlreadyStarted, room_id, room_it->second.members.size()};
    }

    remove_from_room_unlocked(key, record);

    auto& room = rooms_[room_id];
    room.members.insert(key);
    record.room_id = room_id;
    record.in_battle = false;

    return {JoinRoomResult::kOk, record.room_id, room.members.size()};
}

SessionManager::StartBattleOutcome SessionManager::start_battle(const SessionPtr& session) {
    std::scoped_lock lock(mutex_);

    const auto key = session.get();
    const auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return {};
    }

    auto& record = it->second;
    if (!record.authenticated) {
        return {StartBattleResult::kNotAuthenticated, "", 0};
    }

    if (record.room_id.empty()) {
        return {StartBattleResult::kNotInRoom, "", 0};
    }

    auto room_it = rooms_.find(record.room_id);
    if (room_it == rooms_.end()) {
        return {StartBattleResult::kNotInRoom, "", 0};
    }

    auto& room = room_it->second;
    if (room.battle_started) {
        return {StartBattleResult::kAlreadyStarted, record.room_id, room.members.size()};
    }

    if (room.members.size() < 2) {
        return {StartBattleResult::kNotEnoughPlayers, record.room_id, room.members.size()};
    }

    room.battle_started = true;
    for (const auto member_key : room.members) {
        auto member_it = sessions_.find(member_key);
        if (member_it != sessions_.end()) {
            member_it->second.in_battle = true;
        }
    }

    return {StartBattleResult::kOk, record.room_id, room.members.size()};
}

void SessionManager::remove_from_room_unlocked(SessionKey session_key, SessionRecord& record) {
    if (record.room_id.empty()) {
        record.in_battle = false;
        return;
    }

    auto room_it = rooms_.find(record.room_id);
    if (room_it != rooms_.end()) {
        room_it->second.members.erase(session_key);
        if (room_it->second.members.empty()) {
            rooms_.erase(room_it);
        }
    }

    record.room_id.clear();
    record.in_battle = false;
}

}  // namespace game::gateway
