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
