#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "v2/persistence/player_data.h"

namespace {

// -----------------------------------------------------------------------
// PlayerDataStorage CRUD tests
// -----------------------------------------------------------------------

TEST(V2PlayerDataTest, SaveAndLoad) {
    v2::persistence::PlayerDataStorage storage;

    const nlohmann::json data = {
        {"name", "Alice"},
        {"level", 42},
        {"inventory", {"sword", "shield", "potion"}},
    };

    ASSERT_TRUE(storage.save_player("player_alice", data));

    auto loaded = storage.load_player("player_alice");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)["name"], "Alice");
    EXPECT_EQ((*loaded)["level"], 42);
    ASSERT_TRUE((*loaded)["inventory"].is_array());
    EXPECT_EQ((*loaded)["inventory"].size(), 3U);
}

TEST(V2PlayerDataTest, UpdateExisting) {
    v2::persistence::PlayerDataStorage storage;

    ASSERT_TRUE(storage.save_player("player_bob", {{"score", 100}}));
    ASSERT_TRUE(storage.save_player("player_bob", {{"score", 200}}));

    auto loaded = storage.load_player("player_bob");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)["score"], 200);
}

TEST(V2PlayerDataTest, LoadNonExistentReturnsNullopt) {
    v2::persistence::PlayerDataStorage storage;

    auto loaded = storage.load_player("player_nonexistent");
    EXPECT_FALSE(loaded.has_value());
}

TEST(V2PlayerDataTest, DeleteExisting) {
    v2::persistence::PlayerDataStorage storage;

    ASSERT_TRUE(storage.save_player("player_to_delete", {{"temp", true}}));
    EXPECT_TRUE(storage.delete_player("player_to_delete"));
    EXPECT_FALSE(storage.load_player("player_to_delete").has_value());
}

TEST(V2PlayerDataTest, DeleteNonExistentReturnsFalse) {
    v2::persistence::PlayerDataStorage storage;
    EXPECT_FALSE(storage.delete_player("player_never_existed"));
}

TEST(V2PlayerDataTest, DeleteTwiceReturnsFalseSecondTime) {
    v2::persistence::PlayerDataStorage storage;

    ASSERT_TRUE(storage.save_player("player_double_del", {{"x", 1}}));
    EXPECT_TRUE(storage.delete_player("player_double_del"));
    EXPECT_FALSE(storage.delete_player("player_double_del"));
}

TEST(V2PlayerDataTest, SaveAndLoadComplexNestedData) {
    v2::persistence::PlayerDataStorage storage;

    const nlohmann::json data = {
        {"stats", {{"hp", 100}, {"mp", 50}, {"xp", 9999}}},
        {"quests",
         {{{"id", "q1"}, {"completed", true}},
          {{"id", "q2"}, {"completed", false}}}},
        {"metadata", nlohmann::json::object()},
    };

    ASSERT_TRUE(storage.save_player("player_complex", data));

    auto loaded = storage.load_player("player_complex");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)["stats"]["hp"], 100);
    EXPECT_EQ((*loaded)["stats"]["mp"], 50);
    EXPECT_EQ((*loaded)["stats"]["xp"], 9999);
    ASSERT_TRUE((*loaded)["quests"].is_array());
    EXPECT_EQ((*loaded)["quests"][0]["id"], "q1");
    EXPECT_TRUE((*loaded)["quests"][0]["completed"]);
    EXPECT_FALSE((*loaded)["quests"][1]["completed"]);
}

// -----------------------------------------------------------------------
// Cache behaviour tests
// -----------------------------------------------------------------------

TEST(V2PlayerDataTest, CacheHitReturnsSameData) {
    v2::persistence::PlayerDataStorage storage;

    const nlohmann::json data = {{"key", "cache_test"}};
    ASSERT_TRUE(storage.save_player("player_cache", data));

    // First load -- cache miss, reads from backend.
    auto first = storage.load_player("player_cache");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ((*first)["key"], "cache_test");

    // Second load -- should be a cache hit.
    auto second = storage.load_player("player_cache");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ((*second)["key"], "cache_test");
}

TEST(V2PlayerDataTest, CacheUpdatedAfterSave) {
    v2::persistence::PlayerDataStorage storage;

    ASSERT_TRUE(storage.save_player("player_cache_upd", {{"version", 1}}));

    auto first = storage.load_player("player_cache_upd");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ((*first)["version"], 1);

    // Update and reload -- should see new data (cache should be updated).
    ASSERT_TRUE(storage.save_player("player_cache_upd", {{"version", 2}}));

    auto second = storage.load_player("player_cache_upd");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ((*second)["version"], 2);
}

TEST(V2PlayerDataTest, CacheInvalidatedAfterDelete) {
    v2::persistence::PlayerDataStorage storage;

    ASSERT_TRUE(storage.save_player("player_cache_del", {{"data", "value"}}));
    ASSERT_TRUE(storage.load_player("player_cache_del").has_value());

    // Delete should purge the cache entry.
    ASSERT_TRUE(storage.delete_player("player_cache_del"));
    EXPECT_FALSE(storage.load_player("player_cache_del").has_value());
}

// -----------------------------------------------------------------------
// Edge cases
// -----------------------------------------------------------------------

TEST(V2PlayerDataTest, EmptyPlayerId) {
    v2::persistence::PlayerDataStorage storage;

    EXPECT_TRUE(storage.save_player("", {{"empty", true}}));
    auto loaded = storage.load_player("");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE((*loaded)["empty"].get<bool>());
}

TEST(V2PlayerDataTest, EmptyJsonObject) {
    v2::persistence::PlayerDataStorage storage;

    EXPECT_TRUE(storage.save_player("player_empty", nlohmann::json::object()));
    auto loaded = storage.load_player("player_empty");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->empty());
}

TEST(V2PlayerDataTest, LargeJsonPayload) {
    v2::persistence::PlayerDataStorage storage;

    nlohmann::json large;
    for (int i = 0; i < 1000; ++i) {
        large["items"][std::to_string(i)] = "value_" + std::to_string(i);
    }

    EXPECT_TRUE(storage.save_player("player_large", large));
    auto loaded = storage.load_player("player_large");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)["items"].size(), 1000U);
}

TEST(V2PlayerDataTest, MultiplePlayersRoundTrip) {
    v2::persistence::PlayerDataStorage storage;

    for (int i = 0; i < 10; ++i) {
        const auto pid = "player_multi_" + std::to_string(i);
        ASSERT_TRUE(storage.save_player(pid, {{"index", i}}));
    }

    for (int i = 0; i < 10; ++i) {
        const auto pid = "player_multi_" + std::to_string(i);
        auto loaded = storage.load_player(pid);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ((*loaded)["index"], i);
    }
}

}  // anonymous namespace
