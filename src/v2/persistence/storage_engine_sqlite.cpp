#include "v2/persistence/storage_engine_sqlite.h"

#include <spdlog/spdlog.h>

#include <utility>

// The class is only compiled when HAS_SQLITE is defined; the header guards
// the declaration with the same define.

#ifdef HAS_SQLITE

namespace v2::persistence {

// -----------------------------------------------------------------------
// Handle  (RAII)
// -----------------------------------------------------------------------

StorageEngineSQLite::Handle::Handle(sqlite3* db, StorageEngineSQLite* pool)
    : db_(db), pool_(pool) {}

StorageEngineSQLite::Handle::~Handle() {
    if (db_ && pool_) {
        pool_->release(db_);
    }
}

StorageEngineSQLite::Handle::Handle(Handle&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      pool_(std::exchange(other.pool_, nullptr)) {}

StorageEngineSQLite::Handle& StorageEngineSQLite::Handle::operator=(
    Handle&& other) noexcept {
    if (this != &other) {
        if (db_ && pool_) {
            pool_->release(db_);
        }
        db_ = std::exchange(other.db_, nullptr);
        pool_ = std::exchange(other.pool_, nullptr);
    }
    return *this;
}

// -----------------------------------------------------------------------
// StorageEngineSQLite
// -----------------------------------------------------------------------

StorageEngineSQLite::StorageEngineSQLite(Options opts)
    : opts_(std::move(opts)) {
    for (int i = 0; i < opts_.min_connections; ++i) {
        auto* db = create_connection();
        if (db) {
            idle_.push(db);
            ++total_connections_;
        }
    }
    spdlog::info("[StorageEngineSQLite] pool ready ({} conns, db={})",
                 total_connections_, opts_.db_path);
}

StorageEngineSQLite::~StorageEngineSQLite() {
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();

    std::lock_guard lock(mutex_);
    while (!idle_.empty()) {
        sqlite3_close(idle_.front());
        idle_.pop();
    }
}

// -----------------------------------------------------------------------
// acquire / release
// -----------------------------------------------------------------------

StorageEngineSQLite::Handle StorageEngineSQLite::acquire() {
    std::unique_lock lock(mutex_);

    while (idle_.empty() && !stopped_) {
        if (total_connections_ < static_cast<std::size_t>(opts_.max_connections)) {
            // Try to grow the pool.  Release the lock during I/O.
            lock.unlock();
            auto* db = create_connection();
            lock.lock();

            if (db) {
                idle_.push(db);
                ++total_connections_;
                break;
            }
        }

        if (idle_.empty()) {
            cv_.wait(lock);
        } else {
            break;
        }
    }

    if (stopped_ || idle_.empty()) {
        return Handle{};
    }

    auto* db = idle_.front();
    idle_.pop();
    ++active_count_;
    return Handle(db, this);
}

void StorageEngineSQLite::release(sqlite3* db) {
    if (!db) return;
    std::lock_guard lock(mutex_);
    idle_.push(db);
    --active_count_;
    cv_.notify_one();
}

// -----------------------------------------------------------------------
// health_check
// -----------------------------------------------------------------------

bool StorageEngineSQLite::health_check() {
    auto handle = acquire();
    if (!handle) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle.get(), "SELECT 1", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        spdlog::warn("[StorageEngineSQLite] health_check prepare failed: {}",
                     sqlite3_errmsg(handle.get()));
        return false;
    }

    bool healthy = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return healthy;
}

// -----------------------------------------------------------------------
// execute
// -----------------------------------------------------------------------

bool StorageEngineSQLite::execute(
    const std::string& sql,
    std::function<void(sqlite3_stmt*)> bind_fn,
    std::function<bool(sqlite3_stmt*)> row_fn) {

    auto handle = acquire();
    if (!handle) return false;

    const auto start = std::chrono::steady_clock::now();

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle.get(), sql.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        spdlog::error("[StorageEngineSQLite] prepare failed: {}  [sql={}]",
                      sqlite3_errmsg(handle.get()), sql);
        return false;
    }

    if (bind_fn) bind_fn(stmt);

    bool ok = true;
    if (row_fn) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!row_fn(stmt)) break;
        }
    } else {
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
    if (elapsed > opts_.slow_query_threshold.count()) {
        spdlog::warn("[StorageEngineSQLite] slow query ({}ms): {}", elapsed,
                     sql);
    }

    sqlite3_finalize(stmt);

    if (!ok) {
        spdlog::error("[StorageEngineSQLite] step failed: {}  [sql={}]",
                      sqlite3_errmsg(handle.get()), sql);
    }
    return ok;
}

// -----------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------

sqlite3* StorageEngineSQLite::create_connection() {
    sqlite3* db = nullptr;
    const int rc = sqlite3_open(opts_.db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        spdlog::error("[StorageEngineSQLite] sqlite3_open({}) failed: {}",
                      opts_.db_path, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return nullptr;
    }

    if (opts_.wal_mode) {
        configure_wal(db);
    }

    return db;
}

bool StorageEngineSQLite::configure_wal(sqlite3* db) {
    char* errmsg = nullptr;
    const int rc =
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("[StorageEngineSQLite] failed to enable WAL mode: {}",
                     errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    return true;
}

}  // namespace v2::persistence

#endif  // HAS_SQLITE
