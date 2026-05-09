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

TEST(V2BattleProtocolCodecTest, ParsesStableBattleBodiesIntoFields) {
    const auto started =
        v2::gateway::parse_battle_started_body("battle_started:room_id=room_alpha:battle_id=battle_0001");
    ASSERT_TRUE(started.has_value());
    EXPECT_EQ(started->room_id, "room_alpha");
    EXPECT_EQ(started->battle_id, "battle_0001");

    const auto started_state =
        v2::gateway::parse_battle_state_body("battle_state:kind=started:room_id=room_alpha:battle_id=battle_0001");
    ASSERT_TRUE(started_state.has_value());
    EXPECT_EQ(started_state->kind, "started");
    EXPECT_EQ(started_state->room_id, "room_alpha");
    EXPECT_EQ(started_state->battle_id, "battle_0001");
    EXPECT_FALSE(started_state->frame.has_value());

    const auto input_response = v2::gateway::parse_battle_input_response_body("input_seq:seq=7");
    ASSERT_TRUE(input_response.has_value());
    EXPECT_EQ(input_response->input_seq, 7U);

    const auto input_push =
        v2::gateway::parse_battle_input_push_body("battle_input:user_id=owner:seq=3:input=move:3,2");
    ASSERT_TRUE(input_push.has_value());
    EXPECT_EQ(input_push->user_id, "owner");
    EXPECT_EQ(input_push->input_seq, 3U);
    EXPECT_EQ(input_push->input_data, "move:3,2");

    const auto frame = v2::gateway::parse_battle_state_body(
        "battle_state:kind=frame:room_id=room_alpha:battle_id=battle_0001:frame=3:trigger=input:owner:3");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->kind, "frame");
    ASSERT_TRUE(frame->frame.has_value());
    EXPECT_EQ(*frame->frame, 3U);
    ASSERT_TRUE(frame->trigger.has_value());
    EXPECT_EQ(*frame->trigger, "input:owner:3");

    const auto finished = v2::gateway::parse_battle_state_body(
        "battle_state:kind=finished:room_id=room_alpha:battle_id=battle_0001:reason=surrender:user_id=owner");
    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->kind, "finished");
    ASSERT_TRUE(finished->reason.has_value());
    EXPECT_EQ(*finished->reason, "surrender");
    ASSERT_TRUE(finished->user_id.has_value());
    EXPECT_EQ(*finished->user_id, "owner");
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
