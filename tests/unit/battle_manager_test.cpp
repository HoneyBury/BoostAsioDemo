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
