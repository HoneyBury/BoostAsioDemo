#include <gtest/gtest.h>

#include "v2/gateway/gateway_response_parser.h"

TEST(V2GatewayResponseParserTest, ParsesLoginAndRoomResponses) {
    const auto login = v2::gateway::parse_login_response_body("login_ok:player_01");
    ASSERT_TRUE(login.has_value());
    EXPECT_EQ(login->user_id, "player_01");

    const auto created = v2::gateway::parse_room_create_response_body("room_created:room_alpha");
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->room_id, "room_alpha");

    const auto joined = v2::gateway::parse_room_join_response_body("room_joined:room_beta:members=2");
    ASSERT_TRUE(joined.has_value());
    EXPECT_EQ(joined->room_id, "room_beta");
}

TEST(V2GatewayResponseParserTest, ParsesSessionPushBodies) {
    const auto kicked = v2::gateway::parse_session_kicked_body("session_kicked:duplicate_login:room_transferred");
    ASSERT_TRUE(kicked.has_value());
    EXPECT_EQ(kicked->reason, "duplicate_login");
    EXPECT_TRUE(kicked->room_transferred);

    const auto resumed = v2::gateway::parse_session_resumed_body("session_resumed:room_resume:battle=0");
    ASSERT_TRUE(resumed.has_value());
    EXPECT_EQ(resumed->room_id, "room_resume");
    EXPECT_FALSE(resumed->in_battle);
}

TEST(V2GatewayResponseParserTest, ParsesRoomStatePushColonFormat) {
    const auto state = v2::gateway::parse_room_state_push_body(
        "room_state:room_alpha:owner_id:mem1,mem2,mem3:mem3:in_battle");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->room_id, "room_alpha");
    EXPECT_EQ(state->owner_user_id, "owner_id");
    ASSERT_EQ(state->member_ids.size(), 3U);
    EXPECT_EQ(state->member_ids[0], "mem1");
    EXPECT_EQ(state->member_ids[1], "mem2");
    EXPECT_EQ(state->member_ids[2], "mem3");
    ASSERT_EQ(state->ready_ids.size(), 1U);
    EXPECT_EQ(state->ready_ids[0], "mem3");
    EXPECT_TRUE(state->in_battle);
}

TEST(V2GatewayResponseParserTest, ParsesRoomStatePushAltKeyValueFormat) {
    const auto state = v2::gateway::parse_room_state_push_body_alt(
        "room_state:room_id=room_beta:owner_id=owner:members=mem1,mem2:ready=mem1,mem2:in_battle=1");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->room_id, "room_beta");
    EXPECT_EQ(state->owner_user_id, "owner");
    ASSERT_EQ(state->member_ids.size(), 2U);
    EXPECT_EQ(state->member_ids[0], "mem1");
    EXPECT_EQ(state->member_ids[1], "mem2");
    ASSERT_EQ(state->ready_ids.size(), 2U);
    EXPECT_TRUE(state->in_battle);
}

TEST(V2GatewayResponseParserTest, ParsesRoomStatePushAltMinimalFormat) {
    const auto state = v2::gateway::parse_room_state_push_body_alt(
        "room_state:room_id=room_min:owner_id=owner");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->room_id, "room_min");
    EXPECT_EQ(state->owner_user_id, "owner");
    EXPECT_TRUE(state->member_ids.empty());
    EXPECT_TRUE(state->ready_ids.empty());
    EXPECT_FALSE(state->in_battle);
}

TEST(V2GatewayResponseParserTest, RejectsInvalidRoomStateBodies) {
    EXPECT_FALSE(v2::gateway::parse_room_state_push_body("invalid:data").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_state_push_body("room_state:").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_state_push_body_alt("not_room_state:...").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_state_push_body_alt("room_state:room_id=:owner_id=owner").has_value());
}

TEST(V2GatewayResponseParserTest, ParsesErrorResponseBodies) {
    const auto error = v2::gateway::parse_error_response_body("invalid_user_id");
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->reason, "invalid_user_id");

    EXPECT_FALSE(v2::gateway::parse_error_response_body("").has_value());
}

TEST(V2GatewayResponseParserTest, RejectsMalformedLoginAndRoomResponses) {
    EXPECT_FALSE(v2::gateway::parse_login_response_body("bad_prefix:user").has_value());
    EXPECT_FALSE(v2::gateway::parse_login_response_body("login_ok:").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_create_response_body("bad:room").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_create_response_body("room_created:").has_value());
    EXPECT_FALSE(v2::gateway::parse_room_join_response_body("room_joined:").has_value());
}
