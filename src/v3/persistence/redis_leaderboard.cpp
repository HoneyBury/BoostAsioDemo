// v3.2.0: Redis-backed leaderboard implementation.
// Uses Redis ZSET for scores and a hash for display names.
// v3.4.0: Supports RedisConnectionPool for multi-threaded access.

#include "v3/persistence/redis_leaderboard.h"

#include <utility>

namespace v3::persistence {

class RedisLeaderboard::Impl {
public:
    Impl(Config config, std::shared_ptr<RedisConnectionPool> pool = nullptr)
        : pool_(std::move(pool))
        , zset_key_(std::move(config.key))
        , names_key_(zset_key_ + ":names") {
        if (!pool_) {
            direct_client_ = std::make_unique<RedisClient>(std::move(config.redis));
        }
    }

    std::optional<std::int64_t> submit(const std::string& user_id,
                                       const std::string& display_name,
                                       std::int64_t score) {
        auto* client = prep_client();
        if (!client) return std::nullopt;

        if (!client->zadd(zset_key_, static_cast<double>(score), user_id)) {
            return std::nullopt;
        }
        if (!display_name.empty()) {
            if (!client->hset(names_key_, user_id, display_name)) {
                return std::nullopt;
            }
        }

        auto rank = client->zrevrank(zset_key_, user_id);
        if (!rank.has_value()) return std::nullopt;
        return *rank + 1;  // convert 0-based to 1-based
    }

    std::vector<LeaderboardEntry> top_k(std::size_t k) {
        std::vector<LeaderboardEntry> result;
        auto* client = prep_client();
        if (!client || k == 0) return result;

        auto pairs = client->zrevrange_with_scores(
            zset_key_, 0, static_cast<std::int64_t>(k) - 1);

        result.reserve(pairs.size());
        std::int64_t rank = 1;
        for (auto& [user_id, score] : pairs) {
            LeaderboardEntry entry;
            entry.user_id = std::move(user_id);
            entry.score = static_cast<std::int64_t>(score);
            entry.rank = rank++;
            // Resolve display name
            auto name = client->hget(names_key_, entry.user_id);
            if (name.has_value()) entry.display_name = *name;
            result.push_back(std::move(entry));
        }
        return result;
    }

    std::optional<LeaderboardEntry> rank_of(const std::string& user_id) {
        auto* client = prep_client();
        if (!client) return std::nullopt;

        auto rank = client->zrevrank(zset_key_, user_id);
        if (!rank.has_value()) return std::nullopt;

        auto score = client->zscore(zset_key_, user_id);
        if (!score.has_value()) return std::nullopt;

        LeaderboardEntry entry;
        entry.user_id = user_id;
        entry.rank = *rank + 1;
        entry.score = static_cast<std::int64_t>(*score);

        auto name = client->hget(names_key_, user_id);
        if (name.has_value()) entry.display_name = *name;

        return entry;
    }

    std::size_t size() {
        auto* client = prep_client();
        if (!client) return 0;
        auto n = client->zcard(zset_key_);
        return n >= 0 ? static_cast<std::size_t>(n) : 0;
    }

    bool available() {
        auto* client = prep_client();
        return client != nullptr;
    }

private:
    RedisClient* prep_client() {
        if (pool_) {
            conn_holder_ = pool_->acquire();
            return conn_holder_ ? conn_holder_.get() : nullptr;
        }
        if (direct_client_->is_connected()) return direct_client_.get();
        return direct_client_->reconnect() ? direct_client_.get() : nullptr;
    }

    std::shared_ptr<RedisConnectionPool> pool_;
    std::unique_ptr<RedisClient> direct_client_;
    PooledConnection conn_holder_;
    std::string zset_key_;
    std::string names_key_;
};

RedisLeaderboard::RedisLeaderboard(Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RedisLeaderboard::RedisLeaderboard(Config config, std::shared_ptr<RedisConnectionPool> pool)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(pool))) {}

RedisLeaderboard::~RedisLeaderboard() = default;
RedisLeaderboard::RedisLeaderboard(RedisLeaderboard&&) noexcept = default;
RedisLeaderboard& RedisLeaderboard::operator=(RedisLeaderboard&&) noexcept = default;

std::optional<std::int64_t> RedisLeaderboard::submit(
    const std::string& user_id, const std::string& display_name, std::int64_t score) {
    return impl_->submit(user_id, display_name, score);
}

std::vector<LeaderboardEntry> RedisLeaderboard::top_k(std::size_t k) {
    return impl_->top_k(k);
}

std::optional<LeaderboardEntry> RedisLeaderboard::rank_of(const std::string& user_id) {
    return impl_->rank_of(user_id);
}

std::size_t RedisLeaderboard::size() { return impl_->size(); }
bool RedisLeaderboard::available() const { return impl_->available(); }

}  // namespace v3::persistence
