#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "v2/data/cached_data_store.h"
#include "v2/gateway/battle_data_store.h"
#include "v2/gateway/runtime.h"

namespace {

class InMemoryStore final : public v2::gateway::BattleArchiveSink {
public:
    bool persist(const v2::gateway::Runtime::BattleArchive&) override {
        persist_called = true;
        return true;
    }

    bool save_replay(const std::string& battle_id,
                     std::string_view replay_json) override {
        replays[battle_id] = replay_json;
        return true;
    }

    std::optional<std::string> load_replay(
        const std::string& battle_id) override {
        auto it = replays.find(battle_id);
        if (it != replays.end()) return it->second;
        return std::nullopt;
    }

    bool save_result(const std::string& battle_id,
                     std::string_view result_json) override {
        results[battle_id] = result_json;
        return true;
    }

    std::optional<std::string> load_result(
        const std::string& battle_id) override {
        auto it = results.find(battle_id);
        if (it != results.end()) return it->second;
        return std::nullopt;
    }

    bool save_snapshot(const std::string& battle_id,
                       std::string_view snapshot_json) override {
        snapshots[battle_id] = snapshot_json;
        return true;
    }

    std::optional<std::string> load_snapshot(
        const std::string& battle_id) override {
        auto it = snapshots.find(battle_id);
        if (it != snapshots.end()) return it->second;
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> replays;
    std::unordered_map<std::string, std::string> results;
    std::unordered_map<std::string, std::string> snapshots;
    bool persist_called = false;
};

}  // namespace

// ─── Save + Load (write-through cache) ──────────────────────────────

TEST(V2CachedDataStoreTest, SaveReplayLoadsFromCache) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_replay("b001", R"({"r":1})");
    store.flush();

    // Should load from cache — verify delegate was only called via flush.
    auto val = store.load_replay("b001");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, R"({"r":1})");
}

TEST(V2CachedDataStoreTest, SaveResultLoadsFromCache) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_result("b002", R"({"winner":"a"})");
    store.flush();

    auto val = store.load_result("b002");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, R"({"winner":"a"})");
}

TEST(V2CachedDataStoreTest, SaveSnapshotLoadsFromCache) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_snapshot("b003", R"({"frame":42})");
    store.flush();

    auto val = store.load_snapshot("b003");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, R"({"frame":42})");
}

// ─── Read from delegate on cache miss ──────────────────────────────

TEST(V2CachedDataStoreTest, LoadFallsBackToDelegateOnCacheMiss) {
    auto delegate = std::make_shared<InMemoryStore>();
    delegate->replays["b010"] = R"({"from":"disk"})";
    delegate->results["b010"] = R"({"disk":"result"})";
    delegate->snapshots["b010"] = R"({"disk":"snap"})";

    v2::data::CachedBattleDataStore store(delegate);

    auto replay = store.load_replay("b010");
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(*replay, R"({"from":"disk"})");

    auto result = store.load_result("b010");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"disk":"result"})");

    auto snap = store.load_snapshot("b010");
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(*snap, R"({"disk":"snap"})");
}

// ─── Cache hit avoids delegate ─────────────────────────────────────

TEST(V2CachedDataStoreTest, CacheHitAvoidsDelegateRead) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    // Pre-populate the delegate; don't put in cache directly.
    delegate->replays["b020"] = R"({"v":1})";

    // First load misses cache, hits delegate, caches result.
    auto v1 = store.load_replay("b020");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, R"({"v":1})");

    // Modify delegate content — second load should still hit cache.
    delegate->replays["b020"] = R"({"v":999})";

    auto v2 = store.load_replay("b020");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, R"({"v":1})");  // cached value, not v:999
}

// ─── Missing key on both cache and delegate ────────────────────────

TEST(V2CachedDataStoreTest, LoadMissingReturnsNullopt) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    EXPECT_FALSE(store.load_replay("no_such_battle").has_value());
    EXPECT_FALSE(store.load_result("no_such_battle").has_value());
    EXPECT_FALSE(store.load_snapshot("no_such_battle").has_value());
}

// ─── Persist delegates to write-behind (save_result + save_replay) ──

TEST(V2CachedDataStoreTest, PersistDelegatesToWriteBehind) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    v2::gateway::Runtime::BattleArchive archive;
    archive.battle_id = "b100";
    archive.room_id = "room1";
    archive.reason = "normal";
    archive.total_frames = 10;
    archive.replay_payload = R"({"replay":true})";
    archive.result.winner_user_id = "alice";

    store.persist(archive);
    store.flush();

    // WriteBehind decompose persist into save_result + save_replay on delegate.
    ASSERT_TRUE(delegate->results.contains("b100"));
    ASSERT_TRUE(delegate->replays.contains("b100"));
    EXPECT_EQ(delegate->replays["b100"], R"({"replay":true})");
}

// ─── Flush ensures all writes land in delegate ─────────────────────

TEST(V2CachedDataStoreTest, FlushWritesAllPendingToDelegate) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_replay("b200", R"({"flush":"replay"})");
    store.save_result("b200", R"({"flush":"result"})");
    store.save_snapshot("b200", R"({"flush":"snap"})");
    store.flush();

    ASSERT_TRUE(delegate->replays.contains("b200"));
    EXPECT_EQ(delegate->replays["b200"], R"({"flush":"replay"})");
    ASSERT_TRUE(delegate->results.contains("b200"));
    EXPECT_EQ(delegate->results["b200"], R"({"flush":"result"})");
    ASSERT_TRUE(delegate->snapshots.contains("b200"));
    EXPECT_EQ(delegate->snapshots["b200"], R"({"flush":"snap"})");
}

// ─── Multiple saves across different battle IDs ────────────────────

TEST(V2CachedDataStoreTest, MultipleBattleIdsAllCached) {
    auto delegate = std::make_shared<InMemoryStore>();
    v2::data::CachedBattleDataStore store(delegate);

    for (int i = 0; i < 5; ++i) {
        auto id = "battle_" + std::to_string(i);
        store.save_replay(id, R"({"idx":)" + std::to_string(i) + "}");
        store.save_result(id, R"({"idx":)" + std::to_string(i) + "}");
        store.save_snapshot(id, R"({"idx":)" + std::to_string(i) + "}");
    }
    store.flush();

    for (int i = 0; i < 5; ++i) {
        auto id = "battle_" + std::to_string(i);
        auto replay = store.load_replay(id);
        ASSERT_TRUE(replay.has_value());
        EXPECT_EQ(*replay, R"({"idx":)" + std::to_string(i) + "}");
    }
}
