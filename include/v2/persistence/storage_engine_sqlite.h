#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#ifdef HAS_SQLITE
#include <sqlite3.h>

namespace v2::persistence {

/// Thread-safe SQLite connection pool with health checks, slow-query
/// detection, and configurable WAL mode.
///
/// Connections are checked out via the RAII Handle class and
/// automatically returned on destruction.
class StorageEngineSQLite {
public:
    struct Options {
        std::string db_path = "runtime/data/gateway.db";
        int min_connections = 2;
        int max_connections = 8;
        bool wal_mode = true;
        std::chrono::milliseconds slow_query_threshold{500};
    };

    /// RAII handle that returns the sqlite3 connection to the pool on
    /// destruction.
    class Handle {
    public:
        Handle() = default;
        ~Handle();

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept;
        Handle& operator=(Handle&& other) noexcept;

        explicit operator bool() const noexcept { return db_ != nullptr; }
        sqlite3* get() const noexcept { return db_; }

    private:
        friend class StorageEngineSQLite;
        Handle(sqlite3* db, StorageEngineSQLite* pool);
        sqlite3* db_ = nullptr;
        StorageEngineSQLite* pool_ = nullptr;
    };

    explicit StorageEngineSQLite(Options opts = {});
    ~StorageEngineSQLite();

    /// Acquire a connection from the pool (blocks until one is available).
    Handle acquire();

    /// Check that all pooled connections are healthy by executing SELECT 1.
    bool health_check();

    /// Execute a query using an acquired connection.
    /// @param sql        SQL text
    /// @param bind_fn    Optional binder (receives sqlite3_stmt*)
    /// @param row_fn     Optional row handler; return false to stop iteration.
    bool execute(
        const std::string& sql,
        std::function<void(sqlite3_stmt*)> bind_fn = nullptr,
        std::function<bool(sqlite3_stmt*)> row_fn = nullptr);

private:
    void release(sqlite3* db);
    sqlite3* create_connection();
    bool configure_wal(sqlite3* db);

    Options opts_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<sqlite3*> idle_;
    std::size_t active_count_ = 0;
    std::size_t total_connections_ = 0;
    bool stopped_ = false;
};

}  // namespace v2::persistence

#endif  // HAS_SQLITE
