#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "v2/actor/actor.h"
#include "v2/battle/battle_actor.h"
#include "v2/runtime/actor_system.h"

namespace {

// Minimal actor subclass that does nothing — used to verify default
// Snapshotable no-op implementations inherited through Actor.
class MinimalActor final : public v2::actor::Actor {
public:
    void on_message(v2::actor::Message&& /*message*/) override {
        // no-op
    }
};

class RecordingBattleSink final : public v2::battle::BattleEventSink {
public:
    void push(v2::battle::BattleEvent event) override {
        events.push_back(std::move(event));
    }

    std::vector<v2::battle::BattleEvent> events;
};

}  // namespace

// ── Default (no-op) snapshot behaviour ───────────────────────────────

TEST(V2ActorSnapshotTest, ActorDefaultSnapshotReturnsEmpty) {
    MinimalActor actor;
    EXPECT_TRUE(actor.take_snapshot().empty());
}

TEST(V2ActorSnapshotTest, ActorDefaultRestoreReturnsFalse) {
    MinimalActor actor;
    EXPECT_FALSE(actor.restore_from_snapshot("{}"));
    EXPECT_FALSE(actor.restore_from_snapshot(""));
}

// ── BattleActor snapshot ────────────────────────────────────────────

TEST(V2ActorSnapshotTest, BattleActorSnapshotReturnsEmptyJsonWhenWorldDoesNotExist) {
    RecordingBattleSink sink;
    v2::battle::BattleActor actor(sink);
    EXPECT_TRUE(actor.take_snapshot().empty());
}

TEST(V2ActorSnapshotTest, BattleActorRestoreReturnsFalseWithoutWorld) {
    RecordingBattleSink sink;
    v2::battle::BattleActor actor(sink);
    EXPECT_FALSE(actor.restore_from_snapshot("{}"));
}

TEST(V2ActorSnapshotTest, BattleActorSnapshotReturnsJsonWhenWorldExists) {
    v2::runtime::ActorSystem actor_system;
    RecordingBattleSink sink;
    auto actor = std::make_unique<v2::battle::BattleActor>(sink);
    auto* actor_ptr = actor.get();
    auto actor_ref = actor_system.create_actor(std::move(actor));

    v2::actor::Message create;
    create.header.kind = v2::actor::MessageKind::kUser;
    create.payload = v2::battle::CreateBattleMsg{
        .battle_id = "snap_battle_001",
        .room_id = "snap_room",
        .player_ids = {"alice", "bob"},
    };
    actor_ref.tell(std::move(create));
    EXPECT_EQ(actor_system.dispatch_all(), 1U);

    const auto snapshot = actor_ptr->take_snapshot();
    EXPECT_FALSE(snapshot.empty());
    // JSON output must start with '{' for an object
    EXPECT_EQ(snapshot.front(), '{');
    EXPECT_EQ(snapshot.back(), '}');
}

TEST(V2ActorSnapshotTest, BattleActorSnapshotRoundtripPreservesState) {
    v2::runtime::ActorSystem actor_system;
    RecordingBattleSink sink;
    auto actor = std::make_unique<v2::battle::BattleActor>(sink);
    auto* actor_ptr = actor.get();
    auto actor_ref = actor_system.create_actor(std::move(actor));

    // Create battle
    {
        v2::actor::Message create;
        create.header.kind = v2::actor::MessageKind::kUser;
        create.payload = v2::battle::CreateBattleMsg{
            .battle_id = "roundtrip_001",
            .room_id = "roundtrip_room",
            .player_ids = {"alice", "bob"},
        };
        actor_ref.tell(std::move(create));
    }

    // Submit some input
    {
        v2::actor::Message input;
        input.header.kind = v2::actor::MessageKind::kUser;
        input.payload = v2::battle::SubmitBattleInputMsg{
            .user_id = "alice",
            .request_id = 1,
            .input_data = "move:10,20",
            .score = 50,
        };
        actor_ref.tell(std::move(input));
    }

    // Advance a frame
    {
        v2::actor::Message tick;
        tick.header.kind = v2::actor::MessageKind::kUser;
        tick.payload = v2::battle::TickBattleMsg{.trigger = "first_tick"};
        actor_ref.tell(std::move(tick));
    }

    EXPECT_EQ(actor_system.dispatch_all(), 3U);

    // Capture snapshot
    const auto snapshot = actor_ptr->take_snapshot();
    ASSERT_FALSE(snapshot.empty());

    // Create a second actor with the same sink and restore into it
    v2::runtime::ActorSystem actor_system2;
    RecordingBattleSink sink2;
    auto actor2 = std::make_unique<v2::battle::BattleActor>(sink2);
    auto* actor2_ptr = actor2.get();
    auto actor2_ref = actor_system2.create_actor(std::move(actor2));

    // Need a world first — send CreateBattleMsg
    {
        v2::actor::Message create;
        create.header.kind = v2::actor::MessageKind::kUser;
        create.payload = v2::battle::CreateBattleMsg{
            .battle_id = "roundtrip_001",
            .room_id = "roundtrip_room",
            .player_ids = {"alice", "bob"},
        };
        actor2_ref.tell(std::move(create));
    }
    EXPECT_EQ(actor_system2.dispatch_all(), 1U);

    // Verify the snapshot is different from the fresh battle
    const auto fresh_snap = actor2_ptr->take_snapshot();
    EXPECT_NE(snapshot, fresh_snap);

    // Restore from the first actor's snapshot
    ASSERT_TRUE(actor2_ptr->restore_from_snapshot(snapshot));

    // Verify restored state matches the original
    const auto original_state = actor_ptr->state();
    const auto restored_state = actor2_ptr->state();
    EXPECT_EQ(restored_state.battle_id, original_state.battle_id);
    EXPECT_EQ(restored_state.room_id, original_state.room_id);
    EXPECT_EQ(restored_state.frame_number, original_state.frame_number);
    ASSERT_EQ(restored_state.participants.size(), original_state.participants.size());
    EXPECT_EQ(restored_state.participants[0].user_id, original_state.participants[0].user_id);
    EXPECT_EQ(restored_state.participants[1].user_id, original_state.participants[1].user_id);
    EXPECT_EQ(restored_state.lifecycle, original_state.lifecycle);
}

TEST(V2ActorSnapshotTest, BattleActorRestoreReturnsFalseWithMalformedData) {
    v2::runtime::ActorSystem actor_system;
    RecordingBattleSink sink;
    auto actor = std::make_unique<v2::battle::BattleActor>(sink);
    auto* actor_ptr = actor.get();
    auto actor_ref = actor_system.create_actor(std::move(actor));

    v2::actor::Message create;
    create.header.kind = v2::actor::MessageKind::kUser;
    create.payload = v2::battle::CreateBattleMsg{
        .battle_id = "malformed_battle",
        .room_id = "malformed_room",
        .player_ids = {"alice", "bob"},
    };
    actor_ref.tell(std::move(create));
    EXPECT_EQ(actor_system.dispatch_all(), 1U);

    // Malformed JSON should fail restoration
    EXPECT_FALSE(actor_ptr->restore_from_snapshot("not valid json"));

    // Empty string should fail
    EXPECT_FALSE(actor_ptr->restore_from_snapshot(""));
}
