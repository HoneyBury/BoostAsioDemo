#include <gtest/gtest.h>

#include "v2/gateway/gateway_command_parser.h"

TEST(V2GatewayCommandParserTest, ParsesLoginBodyIntoStructuredFields) {
    const auto parsed = v2::gateway::parse_login_command_body("player_01|token:player_01|PlayerOne");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->user_id, "player_01");
    EXPECT_EQ(parsed->token, "token:player_01");
    ASSERT_TRUE(parsed->display_name.has_value());
    EXPECT_EQ(*parsed->display_name, "PlayerOne");
    EXPECT_TRUE(v2::gateway::validate_login_command_body(*parsed));
}

TEST(V2GatewayCommandParserTest, RejectsEmptyLoginUserIdAndInvalidReadyState) {
    const auto parsed = v2::gateway::parse_login_command_body("|token:broken");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(v2::gateway::validate_login_command_body(*parsed));

    EXPECT_EQ(v2::gateway::parse_room_ready_body("true"), true);
    EXPECT_EQ(v2::gateway::parse_room_ready_body("false"), false);
    EXPECT_FALSE(v2::gateway::parse_room_ready_body("ready").has_value());
}

TEST(V2GatewayCommandParserTest, ParsesAndValidatesRoomIdentifiers) {
    const auto room_id = v2::gateway::parse_room_id_body("room_alpha");
    ASSERT_TRUE(room_id.has_value());
    EXPECT_EQ(*room_id, "room_alpha");
    EXPECT_TRUE(v2::gateway::validate_room_id_body("room_beta"));
    EXPECT_FALSE(v2::gateway::validate_room_id_body(""));
    EXPECT_FALSE(v2::gateway::parse_room_id_body("").has_value());
}
