#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "v2/data/lru_cache.h"
#include "v2/gateway/battle_data_store.h"
#include "v2/gateway/runtime.h"

namespace v2::data {

class WriteBehindDataStore;

class CachedBattleDataStore final : public v2::gateway::BattleArchiveSink {
public:
    explicit CachedBattleDataStore(
        std::shared_ptr<v2::gateway::BattleArchiveSink> delegate,
        std::size_t cache_size = 1000);
    ~CachedBattleDataStore() override;

    // BattleArchiveSink
    bool persist(const v2::gateway::Runtime::BattleArchive& archive) override;

    bool save_replay(const std::string& battle_id, std::string_view replay_json) override;
    std::optional<std::string> load_replay(const std::string& battle_id) override;

    bool save_result(const std::string& battle_id, std::string_view result_json) override;
    std::optional<std::string> load_result(const std::string& battle_id) override;

    bool save_snapshot(const std::string& battle_id, std::string_view snapshot_json) override;
    std::optional<std::string> load_snapshot(const std::string& battle_id) override;

    void flush();

private:
    std::shared_ptr<v2::gateway::BattleArchiveSink> delegate_;
    std::unique_ptr<WriteBehindDataStore> write_behind_;
    LruCache<std::string, std::string> replay_cache_;
    LruCache<std::string, std::string> result_cache_;
    LruCache<std::string, std::string> snapshot_cache_;
};

}  // namespace v2::data
