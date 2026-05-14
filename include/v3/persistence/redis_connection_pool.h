#pragma once
// v3.2.0: Redis connection pool for multi-threaded access.
// Provides RAII connection borrowing with automatic return.

#include "v3/persistence/redis_client.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace v3::persistence {

class RedisConnectionPool;

class PooledConnection {
public:
    PooledConnection() = default;
    ~PooledConnection();

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
    PooledConnection(PooledConnection&& other) noexcept;
    PooledConnection& operator=(PooledConnection&& other) noexcept;

    RedisClient* operator->() { return client_; }
    RedisClient& operator*() { return *client_; }
    [[nodiscard]] RedisClient* get() const { return client_; }
    [[nodiscard]] explicit operator bool() const { return client_ != nullptr; }

private:
    friend class RedisConnectionPool;
    PooledConnection(RedisClient* client, RedisConnectionPool* pool);
    void release();

    RedisClient* client_ = nullptr;
    RedisConnectionPool* pool_ = nullptr;
};

class RedisConnectionPool {
public:
    struct Config {
        RedisClient::Config redis;
        std::size_t max_size = 4;
        std::chrono::milliseconds idle_timeout{60'000};
        std::chrono::milliseconds acquire_timeout{5'000};
    };

    explicit RedisConnectionPool(Config config);
    ~RedisConnectionPool();

    RedisConnectionPool(const RedisConnectionPool&) = delete;
    RedisConnectionPool& operator=(const RedisConnectionPool&) = delete;
    RedisConnectionPool(RedisConnectionPool&&) = delete;
    RedisConnectionPool& operator=(RedisConnectionPool&&) = delete;

    /// Acquire a connection. Blocks up to acquire_timeout if pool is exhausted.
    /// Returns empty PooledConnection on timeout or if no Redis is reachable.
    [[nodiscard]] PooledConnection acquire();

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t idle_count() const;

private:
    friend class PooledConnection;
    void release(RedisClient* client);

    struct Slot {
        std::unique_ptr<RedisClient> client;
        bool in_use = false;
    };

    Config config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Slot> slots_;
};

}  // namespace v3::persistence
