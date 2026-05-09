#include <gtest/gtest.h>

#include "v2/battle/runtime_world.h"

#include <unordered_map>

TEST(V2BattleRuntimeWorldTest, TracksFrameTriggerAndParticipantState) {
    auto world = v2::battle::create_battle_world({"alice", "bob"});

    v2::battle::battle_world_apply_input_score(*world, "alice", 7);
    v2::battle::battle_world_apply_input_score(*world, "bob", 3);
    v2::battle::battle_world_mark_offline(*world, "bob");

    EXPECT_EQ(v2::battle::battle_world_tick(*world,
                                            v2::ecs::FrameContext{
                                                .battle_id = "battle_01",
                                                .room_id = "room_01",
                                                .frame_number = 4,
                                                .trigger = "scheduler",
                                            }),
              4U);

    const auto snapshot = v2::battle::battle_world_snapshot(*world);
    EXPECT_EQ(snapshot.clock.frame_number, 4U);
    EXPECT_EQ(snapshot.clock.last_trigger, "scheduler");

    std::unordered_map<std::string, v2::battle::BattleWorldParticipantState> by_user_id;
    for (const auto& participant : snapshot.participants) {
        by_user_id.emplace(participant.user_id, participant);
    }

    ASSERT_EQ(by_user_id.size(), 2U);
    EXPECT_EQ(by_user_id.at("alice").score, 7);
    EXPECT_TRUE(by_user_id.at("alice").online);
    EXPECT_EQ(by_user_id.at("bob").score, 3);
    EXPECT_FALSE(by_user_id.at("bob").online);
}
