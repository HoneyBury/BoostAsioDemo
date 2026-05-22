#include "v2/persistence/player_data.h"

#include "v2/data/lru_cache.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#ifdef HAS_SQLITE
#include <sqlite3.h>
#endif

namespace v2::persistence {

// -----------------------------------------------------------------------
// PlayerDataStorage::Impl  (SQLite backend + LRU cache)
// -----------------------------------------------------------------------

class PlayerDataStorage::Impl {
public:
    Impl() {
#ifdef HAS_SQLITE
        // Use an in-memory database by default; a production deployment would
        // pass a file path through the constructor.  For testing convenience
        // the parameter-less constructor keeps everything in memory so there
        // is no cleanup to manage.
        constexpr const char* kDefaultDb = ":memory:";
        const int rc = sqlite3_open(kDefaultDb, &db_);
        if (rc != SQLITE_OK) {
            spdlog::error("[PlayerDataStorage] sqlite3_open failed: {}",
                          sqlite3_errmsg(db_));
            db_ = nullptr;
            return;
        }
        create_table();
#else
        spdlog::warn(
            "[PlayerDataStorage] compiled without SQLite -- data will not "
            "persist across restarts");
#endif
    }

    ~Impl() {
#ifdef HAS_SQLITE
        if (db_) {
            sqlite3_close(db_);
        }
#endif
    }

    bool save_player(const std::string& player_id, const nlohmann::json& data) {
        const auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        const std::string data_str = data.dump();

#ifdef HAS_SQLITE
        if (db_) {
            const char* sql = "INSERT OR REPLACE INTO player_data "
                              "(player_id, data, updated_at) VALUES (?, ?, ?)";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                spdlog::error("[PlayerDataStorage] prepare failed: {}",
                              sqlite3_errmsg(db_));
                return false;
            }

            sqlite3_bind_text(stmt, 1, player_id.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, data_str.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, now_ts);

            const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);

            if (!ok) {
                spdlog::error("[PlayerDataStorage] insert failed for {}: {}",
                              player_id, sqlite3_errmsg(db_));
                return false;
            }
        }
#else
        (void)now_ts;
#endif

        // Update the LRU cache regardless of the storage backend.
        cache_.put(player_id,
                   CachedEntry{data_str, static_cast<std::int64_t>(now_ts)});
        return true;
    }

    std::optional<nlohmann::json> load_player(const std::string& player_id) {
        // 1. Check the LRU cache first.
        {
            auto cached = cache_.get(player_id);
            if (cached) {
                try {
                    return nlohmann::json::parse(cached->data, nullptr, false);
                } catch (...) {
                    // Corrupt cache entry -- fall through to reload.
                }
            }
        }

#ifdef HAS_SQLITE
        if (!db_) return std::nullopt;

        // 2. Miss -- query the database.
        const char* sql =
            "SELECT data, updated_at FROM player_data WHERE player_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::nullopt;
        }

        sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<nlohmann::json> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const auto data_ptr =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const auto updated_at = sqlite3_column_int64(stmt, 1);
            if (data_ptr) {
                try {
                    result = nlohmann::json::parse(data_ptr, nullptr, false);
                    if (result->is_discarded()) {
                        result.reset();
                    } else {
                        // Seed the cache.
                        cache_.put(
                            player_id,
                            CachedEntry{std::string(data_ptr), updated_at});
                    }
                } catch (...) {
                    result.reset();
                }
            }
        }
        sqlite3_finalize(stmt);
        return result;
#else
        return std::nullopt;
#endif
    }

    bool delete_player(const std::string& player_id) {
        cache_.remove(player_id);

#ifdef HAS_SQLITE
        if (!db_) return false;

        const char* sql = "DELETE FROM player_data WHERE player_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        const int changes = sqlite3_changes(db_);
        return ok && changes > 0;
#else
        return false;
#endif
    }

private:
    struct CachedEntry {
        std::string data;
        std::int64_t updated_at = 0;
    };

#ifdef HAS_SQLITE
    void create_table() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS player_data ("
            "  player_id  TEXT PRIMARY KEY,"
            "  data       TEXT NOT NULL,"
            "  updated_at INTEGER NOT NULL"
            ")";
        char* errmsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            spdlog::error("[PlayerDataStorage] create table failed: {}", errmsg);
            sqlite3_free(errmsg);
        }
    }

    sqlite3* db_ = nullptr;
#endif

    // LRU cache with 1024-entry capacity.
    v2::data::LruCache<std::string, CachedEntry> cache_{1024};
};

// -----------------------------------------------------------------------
// PlayerDataStorage  (delegating pimpl)
// -----------------------------------------------------------------------

PlayerDataStorage::PlayerDataStorage()
    : impl_(std::make_unique<Impl>()) {}

PlayerDataStorage::~PlayerDataStorage() = default;

bool PlayerDataStorage::save_player(const std::string& player_id,
                                     const nlohmann::json& data) {
    return impl_->save_player(player_id, data);
}

std::optional<nlohmann::json> PlayerDataStorage::load_player(
    const std::string& player_id) {
    return impl_->load_player(player_id);
}

bool PlayerDataStorage::delete_player(const std::string& player_id) {
    return impl_->delete_player(player_id);
}

}  // namespace v2::persistence
