#include "game/gateway/session_manager.h"

#include <boost/asio.hpp>

#include <gtest/gtest.h>

namespace {

std::shared_ptr<net::Session> make_session(boost::asio::io_context& io_context) {
    return std::make_shared<net::Session>(net::tcp::socket(io_context));
}

}  // namespace

TEST(SessionManagerTest, TracksAuthenticationAndRoomState) {
    boost::asio::io_context io_context;
    game::gateway::SessionManager manager;

    auto first = make_session(io_context);
    auto second = make_session(io_context);

    manager.add_session(first);
    manager.add_session(second);

    EXPECT_FALSE(manager.is_authenticated(first));

    manager.authenticate(first, "player_a");
    manager.authenticate(second, "player_b");

    EXPECT_TRUE(manager.is_authenticated(first));
    EXPECT_EQ(manager.user_id_of(first).value_or(""), "player_a");

    const auto join_outcome = manager.join_room(first, "room_alpha");
    EXPECT_EQ(join_outcome.result, game::gateway::SessionManager::JoinRoomResult::kOk);
    EXPECT_EQ(join_outcome.player_count, 1U);

    const auto second_join = manager.join_room(second, "room_alpha");
    EXPECT_EQ(second_join.result, game::gateway::SessionManager::JoinRoomResult::kOk);
    EXPECT_EQ(second_join.player_count, 2U);

    const auto battle_outcome = manager.start_battle(first);
    EXPECT_EQ(battle_outcome.result, game::gateway::SessionManager::StartBattleResult::kOk);
    EXPECT_EQ(battle_outcome.player_count, 2U);

    const auto snapshot = manager.snapshot();
    EXPECT_EQ(snapshot.active_sessions, 2U);
    EXPECT_EQ(snapshot.authenticated_sessions, 2U);
    EXPECT_EQ(snapshot.active_rooms, 1U);
    EXPECT_EQ(snapshot.active_battles, 1U);
}

TEST(SessionManagerTest, ReplacesPreviousSessionOnDuplicateLogin) {
    boost::asio::io_context io_context;
    game::gateway::SessionManager manager;

    auto first = make_session(io_context);
    auto second = make_session(io_context);

    manager.add_session(first);
    manager.add_session(second);

    EXPECT_FALSE(manager.authenticate(first, "player_same"));
    auto replaced = manager.authenticate(second, "player_same");

    ASSERT_TRUE(replaced);
    EXPECT_EQ(replaced.get(), first.get());
    EXPECT_FALSE(manager.is_authenticated(first));
    EXPECT_TRUE(manager.is_authenticated(second));
}
