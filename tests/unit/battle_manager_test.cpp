#include "game/battle/battle_manager.h"

#include <gtest/gtest.h>

TEST(BattleManagerTest, StartsBattleOncePerRoom) {
    game::battle::BattleManager battle_manager;

    const auto first = battle_manager.start_battle("room_alpha", {"player_a", "player_b"});
    EXPECT_EQ(first.result, game::battle::BattleManager::StartBattleResult::kOk);
    EXPECT_TRUE(battle_manager.battle_started("room_alpha"));

    const auto duplicate = battle_manager.start_battle("room_alpha", {"player_a", "player_b"});
    EXPECT_EQ(duplicate.result, game::battle::BattleManager::StartBattleResult::kAlreadyStarted);

    const auto input = battle_manager.submit_input("room_alpha", "player_a", "move:left");
    EXPECT_EQ(input.result, game::battle::BattleManager::SubmitInputResult::kOk);
    EXPECT_EQ(input.input.sequence, 1U);

    battle_manager.remove_room("room_alpha");
    EXPECT_FALSE(battle_manager.battle_started("room_alpha"));
}

TEST(BattleManagerTest, SubmitInputUnknownPlayerReturnsNotInBattle) {
    game::battle::BattleManager battle_manager;

    ASSERT_EQ(battle_manager.start_battle("room_x", {"alice", "bob"}).result,
              game::battle::BattleManager::StartBattleResult::kOk);

    const auto wrong = battle_manager.submit_input("room_x", "charlie", "move");
    EXPECT_EQ(wrong.result, game::battle::BattleManager::SubmitInputResult::kPlayerNotInBattle);
}

// T17 / v1.2.1 — 业务边界：无战斗 / 错误房间 / 开局条件 / 结算清理
TEST(BattleManagerTest, SubmitInputWhenBattleNotStartedReturnsBattleNotStarted) {
    game::battle::BattleManager battle_manager;

    const auto no_battle = battle_manager.submit_input("no_such_room", "alice", "move");
    EXPECT_EQ(no_battle.result, game::battle::BattleManager::SubmitInputResult::kBattleNotStarted);

    ASSERT_EQ(battle_manager.start_battle("room_z", {"u1", "u2"}).result,
              game::battle::BattleManager::StartBattleResult::kOk);
    const auto wrong_room = battle_manager.submit_input("other_room", "u1", "move");
    EXPECT_EQ(wrong_room.result, game::battle::BattleManager::SubmitInputResult::kBattleNotStarted);
}

TEST(BattleManagerTest, StartBattleNotEnoughPlayers) {
    game::battle::BattleManager battle_manager;

    EXPECT_EQ(battle_manager.start_battle("r0", {}).result,
              game::battle::BattleManager::StartBattleResult::kNotEnoughPlayers);
    EXPECT_EQ(battle_manager.start_battle("r1", {"solo"}).result,
              game::battle::BattleManager::StartBattleResult::kNotEnoughPlayers);
}

TEST(BattleManagerTest, StartBattleEmptyRoomIdIsNotInRoom) {
    game::battle::BattleManager battle_manager;
    EXPECT_EQ(battle_manager.start_battle("", {"a", "b"}).result,
              game::battle::BattleManager::StartBattleResult::kNotInRoom);
}

TEST(BattleManagerTest, EndBattleUnknownRoomReturnsNullopt) {
    game::battle::BattleManager battle_manager;
    EXPECT_FALSE(battle_manager.end_battle("missing").has_value());
}

TEST(BattleManagerTest, EndBattleClearsBattleAndStopsAcceptingInput) {
    game::battle::BattleManager battle_manager;

    ASSERT_EQ(battle_manager.start_battle("room_end", {"a", "b"}).result,
              game::battle::BattleManager::StartBattleResult::kOk);
    ASSERT_EQ(battle_manager.submit_input("room_end", "a", "x").result,
              game::battle::BattleManager::SubmitInputResult::kOk);

    const auto ended = battle_manager.end_battle("room_end");
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->room_id, "room_end");
    EXPECT_FALSE(battle_manager.battle_started("room_end"));

    const auto after = battle_manager.submit_input("room_end", "a", "y");
    EXPECT_EQ(after.result, game::battle::BattleManager::SubmitInputResult::kBattleNotStarted);
}
