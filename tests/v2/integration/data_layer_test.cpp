#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "v2/data/cached_data_store.h"
#include "v2/gateway/battle_data_store.h"
#include "v2/gateway/runtime.h"

namespace {

class CountingStore final : public v2::gateway::BattleArchiveSink {
public:
    bool persist(const v2::gateway::Runtime::BattleArchive&) override {
        ++persist_count;
        return true;
    }

    bool save_replay(const std::string& id, std::string_view json) override {
        replays[id] = json;
        ++save_replay_count;
        return true;
    }
    std::optional<std::string> load_replay(const std::string& id) override {
        ++load_replay_count;
        auto it = replays.find(id);
        if (it != replays.end()) return it->second;
        return std::nullopt;
    }

    bool save_result(const std::string& id, std::string_view json) override {
        results[id] = json;
        ++save_result_count;
        return true;
    }
    std::optional<std::string> load_result(const std::string& id) override {
        ++load_result_count;
        auto it = results.find(id);
        if (it != results.end()) return it->second;
        return std::nullopt;
    }

    bool save_snapshot(const std::string& id, std::string_view json) override {
        snapshots[id] = json;
        ++save_snapshot_count;
        return true;
    }
    std::optional<std::string> load_snapshot(const std::string& id) override {
        ++load_snapshot_count;
        auto it = snapshots.find(id);
        if (it != snapshots.end()) return it->second;
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> replays;
    std::unordered_map<std::string, std::string> results;
    std::unordered_map<std::string, std::string> snapshots;
    int persist_count = 0;
    int save_replay_count = 0;
    int save_result_count = 0;
    int save_snapshot_count = 0;
    int load_replay_count = 0;
    int load_result_count = 0;
    int load_snapshot_count = 0;
};

}  // namespace

// ─── End-to-end: save → flush → read from cache ─────────────────────

TEST(V2DataLayerIntegrationTest, E2ESaveFlushReadCache) {
    auto delegate = std::make_shared<CountingStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_replay("b001", R"({"r":1})");
    store.save_result("b001", R"({"winner":"a"})");
    store.save_snapshot("b001", R"({"frame":10})");
    store.flush();

    EXPECT_EQ(delegate->save_replay_count, 1);
    EXPECT_EQ(delegate->save_result_count, 1);
    EXPECT_EQ(delegate->save_snapshot_count, 1);

    // Reads hit cache (no delegate calls).
    auto replay = store.load_replay("b001");
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(*replay, R"({"r":1})");

    auto result = store.load_result("b001");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, R"({"winner":"a"})");

    auto snap = store.load_snapshot("b001");
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(*snap, R"({"frame":10})");
}

// ─── Cache miss falls back to delegate ──────────────────────────────

TEST(V2DataLayerIntegrationTest, CacheMissFallsBackToDelegate) {
    auto delegate = std::make_shared<CountingStore>();
    delegate->replays["cold"] = R"({"cold":"data"})";
    delegate->results["cold"] = R"({"cold":"result"})";
    delegate->snapshots["cold"] = R"({"cold":"snap"})";

    v2::data::CachedBattleDataStore store(delegate);

    auto replay = store.load_replay("cold");
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(*replay, R"({"cold":"data"})");
    EXPECT_EQ(delegate->load_replay_count, 1);

    // Second read hits cache — no additional delegate call.
    auto replay2 = store.load_replay("cold");
    ASSERT_TRUE(replay2.has_value());
    EXPECT_EQ(delegate->load_replay_count, 1);  // unchanged
}

// ─── Async write + read before flush returns cached value ───────────

TEST(V2DataLayerIntegrationTest, ReadBeforeFlushReturnsCachedWrite) {
    auto delegate = std::make_shared<CountingStore>();
    v2::data::CachedBattleDataStore store(delegate);

    store.save_replay("b003", R"({"immediate":"val"})");
    // Don't flush — data is in cache but not yet in delegate.

    auto replay = store.load_replay("b003");
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(*replay, R"({"immediate":"val"})");

    // Delegate shouldn't have received the write yet.
    EXPECT_EQ(delegate->save_replay_count, 0);
}

// ─── Persist round-trip ─────────────────────────────────────────────

TEST(V2DataLayerIntegrationTest, PersistRoundTrip) {
    auto delegate = std::make_shared<CountingStore>();
    v2::data::CachedBattleDataStore store(delegate);

    v2::gateway::Runtime::BattleArchive archive;
    archive.battle_id = "b100";
    archive.room_id = "room1";
    archive.reason = "normal";
    archive.total_frames = 50;
    archive.participant_user_ids = {"alice", "bob"};
    archive.replay_payload = R"([{"frame":1},{"frame":2}])";
    archive.result.winner_user_id = "alice";

    store.persist(archive);
    store.flush();

    // WriteBehind decompose persist into save_result + save_replay on delegate.
    EXPECT_EQ(delegate->save_result_count, 1);
    EXPECT_EQ(delegate->save_replay_count, 1);
    ASSERT_TRUE(delegate->results.contains("b100"));
    ASSERT_TRUE(delegate->replays.contains("b100"));
}

// ─── Multiple battle IDs ────────────────────────────────────────────

TEST(V2DataLayerIntegrationTest, MultipleBattlesEndToEnd) {
    auto delegate = std::make_shared<CountingStore>();
    v2::data::CachedBattleDataStore store(delegate);

    constexpr int kBattleCount = 20;
    for (int i = 0; i < kBattleCount; ++i) {
        auto id = "battle_" + std::to_string(i);
        store.save_replay(id, R"({"idx":)" + std::to_string(i) + "}");
        store.save_result(id, R"({"idx":)" + std::to_string(i) + "}");
        store.save_snapshot(id, R"({"idx":)" + std::to_string(i) + "}");
    }
    store.flush();

    EXPECT_EQ(delegate->save_replay_count, kBattleCount);
    EXPECT_EQ(delegate->save_result_count, kBattleCount);
    EXPECT_EQ(delegate->save_snapshot_count, kBattleCount);

    // All reads hit cache.
    for (int i = 0; i < kBattleCount; ++i) {
        auto id = "battle_" + std::to_string(i);
        auto replay = store.load_replay(id);
        ASSERT_TRUE(replay.has_value()) << "missing replay: " << id;

        auto result = store.load_result(id);
        ASSERT_TRUE(result.has_value()) << "missing result: " << id;

        auto snap = store.load_snapshot(id);
        ASSERT_TRUE(snap.has_value()) << "missing snapshot: " << id;
    }
}

// ─── Cache eviction on overflow ─────────────────────────────────────

TEST(V2DataLayerIntegrationTest, CacheEvictsWhenFull) {
    auto delegate = std::make_shared<CountingStore>();
    v2::data::CachedBattleDataStore store(delegate, /*cache_size=*/5);

    // Write 10 items (b_0..b_9) — cache holds last 5 (b_5..b_9).
    for (int i = 0; i < 10; ++i) {
        store.save_replay("b_" + std::to_string(i), std::to_string(i));
    }
    store.flush();

    // Load in reverse: b_9..b_5 hit cache, b_4..b_0 miss and fall back.
    for (int i = 9; i >= 0; --i) {
        auto val = store.load_replay("b_" + std::to_string(i));
        ASSERT_TRUE(val.has_value()) << "missing b_" << i;
        EXPECT_EQ(*val, std::to_string(i));
    }

    // 5 cache misses: b_4, b_3, b_2, b_1, b_0.
    EXPECT_EQ(delegate->load_replay_count, 5);
}
