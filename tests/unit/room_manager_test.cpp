#include "game/battle/battle_manager.h"
#include "game/room/room_manager.h"

#include <boost/asio.hpp>

#include <gtest/gtest.h>

namespace {

std::shared_ptr<net::Session> make_session(boost::asio::io_context& io_context) {
    return std::make_shared<net::Session>(net::tcp::socket(io_context));
}

}  // namespace

TEST(RoomManagerTest, SupportsCreateJoinOwnerAndReadyState) {
    boost::asio::io_context io_context;
    game::room::RoomManager room_manager;

    auto owner = make_session(io_context);
    auto member = make_session(io_context);

    const auto [create_result, create_outcome] = room_manager.create_room(owner, "room_alpha");
    EXPECT_EQ(create_result, game::room::RoomManager::CreateRoomResult::kOk);
    EXPECT_EQ(create_outcome.player_count, 1U);

    const auto [join_result, join_outcome] = room_manager.join_room(member, "room_alpha");
    EXPECT_EQ(join_result, game::room::RoomManager::JoinRoomResult::kOk);
    EXPECT_EQ(join_outcome.player_count, 2U);

    const auto [ready_result, ready_outcome] = room_manager.set_ready(member, true);
    EXPECT_EQ(ready_result, game::room::RoomManager::ReadyResult::kOk);
    EXPECT_EQ(ready_outcome.room_id, "room_alpha");

    const auto snapshot = room_manager.room_snapshot("room_alpha");
    ASSERT_TRUE(snapshot);
    ASSERT_TRUE(snapshot->owner);
    EXPECT_EQ(snapshot->owner.get(), owner.get());
    EXPECT_EQ(snapshot->members.size(), 2U);

    room_manager.remove_session(owner);
    const auto snapshot_after_owner_leave = room_manager.room_snapshot("room_alpha");
    ASSERT_TRUE(snapshot_after_owner_leave);
    ASSERT_TRUE(snapshot_after_owner_leave->owner);
    EXPECT_EQ(snapshot_after_owner_leave->owner.get(), member.get());
}

TEST(RoomManagerTest, JoinAndReadyRejectedWhenBattleManagerMarksRoomInBattle) {
    boost::asio::io_context io_context;
    game::battle::BattleManager battle_manager;
    game::room::RoomManager room_manager;

    room_manager.set_battle_active_query([&battle_manager](const std::string& room_id) {
        return battle_manager.battle_started(room_id);
    });

    auto owner = make_session(io_context);
    auto joiner = make_session(io_context);

    EXPECT_EQ(room_manager.create_room(owner, "battle_room").first, game::room::RoomManager::CreateRoomResult::kOk);

    ASSERT_EQ(battle_manager.start_battle("battle_room", {"p1", "p2"}).result,
              game::battle::BattleManager::StartBattleResult::kOk);

    const auto join = room_manager.join_room(joiner, "battle_room");
    EXPECT_EQ(join.first, game::room::RoomManager::JoinRoomResult::kRoomInBattle);

    const auto ready = room_manager.set_ready(owner, true);
    EXPECT_EQ(ready.first, game::room::RoomManager::ReadyResult::kRoomInBattle);

    ASSERT_TRUE(room_manager.room_snapshot("battle_room"));
    EXPECT_TRUE(room_manager.room_snapshot("battle_room")->battle_started);
}

TEST(RoomManagerTest, TransferSessionPreservesMemberUserId) {
    boost::asio::io_context io_context;
    game::room::RoomManager room_manager;

    auto old_session = make_session(io_context);
    auto new_session = make_session(io_context);

    ASSERT_EQ(room_manager.create_room(old_session, "transfer_room").first,
              game::room::RoomManager::CreateRoomResult::kOk);
    room_manager.set_member_user_id(old_session, "sticky_uid");

    ASSERT_TRUE(room_manager.transfer_session(old_session, new_session));

    const auto snap = room_manager.room_snapshot_of(new_session);
    ASSERT_TRUE(snap);
    ASSERT_EQ(snap->members.size(), 1U);
    EXPECT_EQ(snap->members.front().member_user_id, "sticky_uid");
    EXPECT_EQ(snap->members.front().session.get(), new_session.get());
}
