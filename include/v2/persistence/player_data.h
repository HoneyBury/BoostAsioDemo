#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace v2::persistence {

/// Persistent player data storage backed by SQLite with an in-memory LRU
/// cache (1024 entries).
///
/// All public methods are thread-safe.
///
/// Table schema:
///   CREATE TABLE IF NOT EXISTS player_data (
///       player_id   TEXT PRIMARY KEY,
///       data        TEXT NOT NULL,
///       updated_at  INTEGER NOT NULL
///   );
class PlayerDataStorage {
public:
    PlayerDataStorage();
    ~PlayerDataStorage();

    /// Persist (insert or replace) player data. Returns true on success.
    bool save_player(const std::string& player_id,
                     const nlohmann::json& data);

    /// Load player data. Returns std::nullopt if the player does not exist.
    std::optional<nlohmann::json> load_player(
        const std::string& player_id);

    /// Delete a player record. Returns true if a record was removed.
    bool delete_player(const std::string& player_id);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace v2::persistence
