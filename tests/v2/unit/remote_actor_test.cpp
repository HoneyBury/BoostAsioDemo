// v3.0.0 Phase 14: Remote Actor Transport + Consistent Hash tests

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "v2/runtime/actor_system.h"
#include "v3/cluster/remote_actor.h"
#include "v3/cluster/consistent_hash.h"

using namespace v3::cluster;

// ─── RemoteActorRef ──────────────────────────────────────────────────────

TEST(RemoteActorTest, LocalRefIsLocal) {
    NodeId node{.host="10.0.0.1", .node_name="node-1"};
    RemoteActorRef ref(v2::actor::ActorRef{}, node);
    EXPECT_TRUE(ref.is_local());
    EXPECT_EQ(ref.node().node_name, "node-1");
}

TEST(RemoteActorTest, RemoteRefIsNotLocal) {
    NodeId node{.host="10.0.0.2", .node_name="node-2"};
    auto ref = RemoteActorRef::remote(42, node);
    EXPECT_FALSE(ref.is_local());
    EXPECT_EQ(ref.remote_id(), 42U);
    EXPECT_EQ(ref.node().node_name, "node-2");
    EXPECT_FALSE(ref.local_ref().has_value());
}

TEST(RemoteActorTest, SerializeDeserializePreservesEnvelopeFieldsAndStringPayload) {
    v2::actor::Message msg;
    msg.header.kind = v2::actor::MessageKind::kUser;
    msg.header.trace_id = 0xAABBCCDD;
    msg.header.request_id = 42;
    msg.header.source_actor = 1001;
    msg.header.target_actor = 2002;
    msg.header.created_at = 123456789;
    msg.payload = std::string("match_join:alice");

    auto encoded = RemoteActorTransport::serialize(msg);
    auto decoded = RemoteActorTransport::deserialize(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.kind, v2::actor::MessageKind::kUser);
    EXPECT_EQ(decoded->header.trace_id, 0xAABBCCDDU);
    EXPECT_EQ(decoded->header.request_id, 42U);
    EXPECT_EQ(decoded->header.source_actor, 1001U);
    EXPECT_EQ(decoded->header.target_actor, 2002U);
    EXPECT_EQ(decoded->header.created_at, 123456789U);

    const auto* payload = std::get_if<std::string>(&decoded->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(*payload, "match_join:alice");
}

TEST(RemoteActorTest, DeserializeRejectsTruncatedEnvelope) {
    auto decoded = RemoteActorTransport::deserialize("short");
    EXPECT_FALSE(decoded.has_value());
}

// ─── ActorLocationRegistry ───────────────────────────────────────────────

TEST(ActorLocationTest, RegisterAndLocate) {
    ActorLocationRegistry registry;
    NodeId node{.host="10.0.0.1", .node_name="node-1"};
    registry.register_actor(100, node);
    EXPECT_EQ(registry.actor_count(), 1U);

    auto loc = registry.locate(100);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->node_name, "node-1");
}

TEST(ActorLocationTest, RelocateMovesActor) {
    ActorLocationRegistry registry;
    NodeId n1{.host="10.0.0.1", .node_name="node-1"};
    NodeId n2{.host="10.0.0.2", .node_name="node-2"};

    registry.register_actor(200, n1);
    registry.relocate(200, n2);

    auto loc = registry.locate(200);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->node_name, "node-2");
}

TEST(ActorLocationTest, UnregisterRemovesActor) {
    ActorLocationRegistry registry;
    NodeId node{.host="10.0.0.1", .node_name="node-1"};
    registry.register_actor(300, node);
    registry.unregister_actor(300);
    EXPECT_FALSE(registry.locate(300).has_value());
    EXPECT_EQ(registry.actor_count(), 0U);
}

TEST(ActorLocationTest, ActorsOnNodeCount) {
    ActorLocationRegistry registry;
    NodeId n1{.host="10.0.0.1", .node_name="node-1"};
    NodeId n2{.host="10.0.0.2", .node_name="node-2"};

    registry.register_actor(1, n1);
    registry.register_actor(2, n1);
    registry.register_actor(3, n2);

    EXPECT_EQ(registry.actors_on_node(n1), 2U);
    EXPECT_EQ(registry.actors_on_node(n2), 1U);
}

// ─── Consistent Hash ─────────────────────────────────────────────────────

TEST(ConsistentHashTest, SingleNodeAlwaysReturnsSameNode) {
    ConsistentHashRing ring;
    ring.add_node("backend-1");

    // Same key should always return same node
    auto n1 = ring.lookup("room_001");
    auto n2 = ring.lookup("room_001");
    EXPECT_EQ(n1, "backend-1");
    EXPECT_EQ(n2, n1);
}

TEST(ConsistentHashTest, LookupDistributesKeysAcrossNodes) {
    ConsistentHashRing ring;
    ring.add_node("backend-1");
    ring.add_node("backend-2");
    ring.add_node("backend-3");

    // Multiple keys should be distributed
    std::map<std::string, int> counts;
    for (int i = 0; i < 1000; ++i) {
        auto node = ring.lookup("room_" + std::to_string(i));
        counts[node]++;
    }

    // Each node should get roughly 1/3 of keys (± reasonable margin)
    EXPECT_GE(counts.size(), 2U);  // at least 2 nodes got keys
    for (auto& [node, count] : counts) {
        EXPECT_GT(count, 100) << node << " has too few keys";
        EXPECT_LT(count, 600) << node << " has too many keys";
    }
}

TEST(ConsistentHashTest, VirtualNodeCount) {
    ConsistentHashRing ring(ConsistentHashRing::Config{.virtual_nodes = 100});
    ring.add_node("node-a");
    EXPECT_EQ(ring.size(), 100U);  // 100 virtual nodes

    ring.add_node("node-b");
    EXPECT_EQ(ring.size(), 200U);  // 200 virtual nodes total
}

TEST(ConsistentHashTest, RemoveNodeCleansVirtualNodes) {
    ConsistentHashRing ring(ConsistentHashRing::Config{.virtual_nodes = 50});
    ring.add_node("temp-node");
    EXPECT_EQ(ring.size(), 50U);

    ring.remove_node("temp-node");
    EXPECT_EQ(ring.size(), 0U);
}

TEST(ConsistentHashTest, RemapFractionIsOneOverN) {
    ConsistentHashRing ring(ConsistentHashRing::Config{.virtual_nodes = 150});
    ring.add_node("a");
    ring.add_node("b");
    ring.add_node("c");
    // 3 nodes → ~1/3 remap on removal
    auto fraction = ring.remap_fraction();
    EXPECT_NEAR(fraction, 1.0 / 3.0, 0.1);
}

TEST(ConsistentHashTest, LookupNReturnsReplicas) {
    ConsistentHashRing ring(ConsistentHashRing::Config{.virtual_nodes = 150});
    ring.add_node("n1");
    ring.add_node("n2");
    ring.add_node("n3");

    auto replicas = ring.lookup_n("battle_0001", 2);
    ASSERT_EQ(replicas.size(), 2U);
    EXPECT_NE(replicas[0], replicas[1]);  // different nodes
}

// ─── ShardRouter ─────────────────────────────────────────────────────────

TEST(ShardRouterTest, RoomAndBattleRouting) {
    ShardRouter router;
    router.add_backend("room-1");
    router.add_backend("room-2");

    auto r1 = router.route_room("room_alpha");
    auto r2 = router.route_room("room_alpha");
    EXPECT_EQ(r1, r2);  // same room always same node

    auto b1 = router.route_battle("battle_001");
    EXPECT_FALSE(b1.empty());
}

TEST(ShardRouterTest, AddRemoveBackend) {
    ShardRouter router;
    router.add_backend("b1");
    router.add_backend("b2");
    EXPECT_EQ(router.room_ring().size(), 300U);  // 2 × 150 virtual nodes

    router.remove_backend("b1");
    EXPECT_EQ(router.room_ring().size(), 150U);
}

// ─── RemoteActorRef::tell() tests ─────────────────────────────────────────

namespace {

/// Test actor that records string payloads from received messages.
class TellTestActor final : public v2::actor::Actor {
public:
    explicit TellTestActor(std::vector<std::string>& received)
        : received_(received) {}

    void on_message(v2::actor::Message&& message) override {
        if (const auto* text = std::get_if<std::string>(&message.payload)) {
            received_.push_back(*text);
        }
    }

private:
    std::vector<std::string>& received_;
};

}  // anonymous namespace

TEST(RemoteActorTest, TellLocalDeliversToLocalRef) {
    std::vector<std::string> received;
    v2::runtime::ActorSystem actor_system;
    auto actor_ref = actor_system.create_actor(
        std::make_unique<TellTestActor>(received));

    NodeId node{.host = "127.0.0.1", .node_name = "local-node"};
    RemoteActorRef remote_ref(actor_ref, node);

    v2::actor::Message msg;
    msg.header.kind = v2::actor::MessageKind::kUser;
    msg.payload = std::string("local_delivery");

    remote_ref.tell(std::move(msg));

    EXPECT_EQ(actor_system.dispatch_all(), 1U);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0], "local_delivery");
}

TEST(RemoteActorTest, TellRemoteWithoutTransportDoesNotCrash) {
    auto ref = RemoteActorRef::remote(42, NodeId{.host = "10.0.0.1", .node_name = "node-1"});

    v2::actor::Message msg;
    msg.header.kind = v2::actor::MessageKind::kUser;
    msg.payload = std::string("hello");

    // Should not crash; should log a warning and return.
    ref.tell(std::move(msg));
}

TEST(RemoteActorTest, TellRemoteWithTransportRoutes) {
    auto registry = std::make_shared<ActorLocationRegistry>();
    auto router = std::make_shared<ClusterRouter>();
    RemoteActorTransport transport(registry, router);

    NodeId target_node{.host = "10.0.0.2", .node_name = "node-2"};
    NodeId captured_node;
    std::string captured_data;

    transport.set_sender(
        [&](const NodeId& node, const std::string& data) -> bool {
            captured_node = node;
            captured_data = data;
            return true;
        });

    RemoteActorRef::set_default_transport(&transport);

    auto ref = RemoteActorRef::remote(42, target_node);

    v2::actor::Message msg;
    msg.header.kind = v2::actor::MessageKind::kUser;
    msg.header.request_id = 99;
    msg.header.source_actor = 100;
    msg.header.target_actor = 42;
    msg.payload = std::string("remote_hello");

    ref.tell(std::move(msg));

    EXPECT_EQ(captured_node.node_name, "node-2");
    EXPECT_FALSE(captured_data.empty());

    // Verify the serialized data can be deserialized and preserves fields.
    auto decoded = RemoteActorTransport::deserialize(captured_data);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.request_id, 99U);
    EXPECT_EQ(decoded->header.source_actor, 100U);
    EXPECT_EQ(decoded->header.target_actor, 42U);

    const auto* payload = std::get_if<std::string>(&decoded->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(*payload, "remote_hello");

    // Clean up global state.
    RemoteActorRef::set_default_transport(nullptr);
}
