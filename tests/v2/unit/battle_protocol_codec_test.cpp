#include "v2/gateway/battle_protocol_codec.h"

#include <gtest/gtest.h>

TEST(V2BattleProtocolCodecTest, ParsesRequestedFinishReasons) {
    EXPECT_EQ(v2::gateway::parse_battle_finish_request("finish:"), v2::battle::BattleFinishReason::kFinished);
    EXPECT_EQ(v2::gateway::parse_battle_finish_request("finish:surrender"),
              v2::battle::BattleFinishReason::kSurrender);
    EXPECT_EQ(v2::gateway::parse_battle_finish_request("finish:timeout"),
              v2::battle::BattleFinishReason::kTimeout);
    EXPECT_EQ(v2::gateway::parse_battle_finish_request("finish:custom"),
              v2::battle::BattleFinishReason::kFinished);
    EXPECT_FALSE(v2::gateway::parse_battle_finish_request("move:1,2").has_value());
}

TEST(V2BattleProtocolCodecTest, FormatsBattleBodiesWithStableSchema) {
    const auto started = v2::gateway::format_battle_started_body("room_alpha", "battle_0001");
    EXPECT_EQ(started, "battle_started:room_id=room_alpha:battle_id=battle_0001");

    const auto state = v2::gateway::format_battle_state_body("room_alpha", "battle_0001");
    EXPECT_EQ(state, "battle_state:kind=started:room_id=room_alpha:battle_id=battle_0001");

    const auto input_response = v2::gateway::format_battle_input_response_body(3);
    EXPECT_EQ(input_response, "input_seq:seq=3");

    const auto input_push = v2::gateway::format_battle_input_push_body("owner", 3, "move:3,2");
    EXPECT_EQ(input_push, "battle_input:user_id=owner:seq=3:input=move:3,2");

    const auto end_accepted =
        v2::gateway::format_battle_end_accepted_body(v2::battle::BattleFinishReason::kSurrender);
    EXPECT_EQ(end_accepted, "battle_end_accepted:surrender");

    const v2::battle::BattleFrameAdvancedMsg frame{
        .battle_id = "battle_0001",
        .room_id = "room_alpha",
        .frame_number = 3,
        .trigger = "input:owner:3",
    };
    EXPECT_EQ(v2::gateway::format_battle_frame_body(frame),
              "battle_state:kind=frame:room_id=room_alpha:battle_id=battle_0001:frame=3:trigger=input:owner:3");

    const v2::battle::BattleFinishedMsg finished{
        .battle_id = "battle_0001",
        .room_id = "room_alpha",
        .reason = v2::battle::BattleFinishReason::kFrameLimitReached,
        .triggering_user_id = "input:owner:3",
    };
    EXPECT_EQ(v2::gateway::format_battle_finished_body(finished),
              "battle_state:kind=finished:room_id=room_alpha:battle_id=battle_0001:reason=frame_limit_reached:user_id=input:owner:3");
}
