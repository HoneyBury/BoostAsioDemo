#include "v2/gateway/battle_wire_parser.h"

#include <gtest/gtest.h>

TEST(V2BattleWireParserTest, ParsesAndValidatesBattleWireBodies) {
    const auto started = v2::gateway::parse_battle_wire_body(
        "battle_started:room_id=room_alpha:battle_id=battle_0001");
    ASSERT_TRUE(started.has_value());
    EXPECT_EQ(v2::gateway::battle_wire_body_kind(*started), v2::gateway::BattleWireBodyKind::kStarted);
    EXPECT_TRUE(v2::gateway::validate_battle_wire_body(*started));

    const auto frame = v2::gateway::parse_battle_wire_body(
        "battle_state:kind=frame:room_id=room_alpha:battle_id=battle_0001:frame=2:trigger=input:owner:2");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(v2::gateway::battle_wire_body_kind(*frame), v2::gateway::BattleWireBodyKind::kState);
    EXPECT_TRUE(v2::gateway::validate_battle_wire_body(*frame));

    const auto finish_request = v2::gateway::parse_battle_wire_body("finish:surrender");
    ASSERT_TRUE(finish_request.has_value());
    EXPECT_EQ(v2::gateway::battle_wire_body_kind(*finish_request),
              v2::gateway::BattleWireBodyKind::kFinishRequest);
    EXPECT_TRUE(v2::gateway::validate_battle_wire_body(*finish_request));

    const auto end_accepted = v2::gateway::parse_battle_wire_body("battle_end_accepted:surrender");
    ASSERT_TRUE(end_accepted.has_value());
    EXPECT_EQ(v2::gateway::battle_wire_body_kind(*end_accepted),
              v2::gateway::BattleWireBodyKind::kEndAccepted);
    EXPECT_TRUE(v2::gateway::validate_battle_wire_body(*end_accepted));
}

TEST(V2BattleWireParserTest, RejectsInvalidBattleWireBodies) {
    const auto broken_state =
        v2::gateway::parse_battle_wire_body("battle_state:kind=frame:room_id=room_alpha:battle_id=battle_0001");
    ASSERT_TRUE(broken_state.has_value());
    EXPECT_FALSE(v2::gateway::validate_battle_wire_body(*broken_state));

    const auto broken_input = v2::gateway::parse_battle_wire_body("battle_input:user_id=:seq=0:input=");
    ASSERT_TRUE(broken_input.has_value());
    EXPECT_FALSE(v2::gateway::validate_battle_wire_body(*broken_input));
}
