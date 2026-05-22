// v3.0.0 E2E: Cluster Router integration tests.
//
// Tests cover:
//   - Round-robin load balancing across multiple instances
//   - Healthy/unhealthy state transitions
//   - Health check auto-detection via ClusterRouter::run_health_checks()
//   - GatewayServiceBridge::resolve_backend() integration
//
// These tests operate on the ClusterRouter and GatewayServiceBridge
// directly without requiring real socket connections.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "v2/gateway/gateway_service_bridge.h"
#include "v3/cluster/cluster_router.h"

using namespace v3::cluster;
using namespace v2::gateway;

// ─── Test 1: Round-robin with multiple instances ──────────────────────────

TEST(ClusterRouterE2ETest, RoundRobinAcrossMultipleLoginInstances) {
    ClusterRouter router;

    // Register 2 login instances
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9302, .node_name = "login-1"},
        .service_name = "login",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.2", .port = 9302, .node_name = "login-2"},
        .service_name = "login",
    });

    // First two discovers should return different instances (round-robin)
    auto d1 = router.discover("login");
    ASSERT_TRUE(d1.has_value());
    auto d2 = router.discover("login");
    ASSERT_TRUE(d2.has_value());

    // Round-robin: they should be different
    EXPECT_NE(d1->node.node_name, d2->node.node_name);

    // Third discover wraps around back to the first
    auto d3 = router.discover("login");
    ASSERT_TRUE(d3.has_value());
    EXPECT_EQ(d1->node.node_name, d3->node.node_name);
}

TEST(ClusterRouterE2ETest, RoundRobinSkipsUnhealthyInstances) {
    ClusterRouter router;

    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9302, .node_name = "login-1"},
        .service_name = "login",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.2", .port = 9302, .node_name = "login-2"},
        .service_name = "login",
    });

    // Mark login-1 as unhealthy
    NodeId n1{.host = "10.0.0.1", .port = 9302, .node_name = "login-1"};
    router.mark_unhealthy("login", n1);

    // Both discovers should return login-2
    auto d1 = router.discover("login");
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(d1->node.node_name, "login-2");

    auto d2 = router.discover("login");
    ASSERT_TRUE(d2.has_value());
    EXPECT_EQ(d2->node.node_name, "login-2");
}

// ─── Test 2: Healthy/unhealthy transitions ────────────────────────────────

TEST(ClusterRouterE2ETest, UnhealthyNodeExcludedThenRecovered) {
    ClusterRouter router;

    NodeId node{.host = "10.0.0.1", .port = 9302, .node_name = "login-1"};
    router.register_service(ServiceInstance{
        .node = node,
        .service_name = "login",
    });

    // Initially healthy
    EXPECT_TRUE(router.discover("login").has_value());

    // Mark unhealthy → no longer discoverable
    router.mark_unhealthy("login", node);
    EXPECT_FALSE(router.discover("login").has_value());

    // Mark healthy again → discoverable
    router.mark_healthy("login", node);
    EXPECT_TRUE(router.discover("login").has_value());
}

TEST(ClusterRouterE2ETest, DrainNodeExcludedUntilTimeout) {
    ClusterRouter router(HealthCheckConfig{
        .interval = std::chrono::milliseconds(100),
        .timeout = std::chrono::milliseconds(50),
        .failure_threshold = 1,
        .recovery_threshold = 1,
        .drain_timeout = std::chrono::milliseconds(50),
    });

    NodeId node{.host = "10.0.0.1", .port = 9302, .node_name = "login-1"};
    router.register_service(ServiceInstance{
        .node = node,
        .service_name = "login",
    });

    // Start draining → not discoverable
    router.start_drain("login", node);
    EXPECT_FALSE(router.discover("login").has_value());

    // Wait for drain timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Run health checks → drain timeout should mark it unhealthy
    router.run_health_checks();

    // Still not discoverable (now unhealthy instead of draining)
    EXPECT_FALSE(router.discover("login").has_value());
}

// ─── Test 3: Health check auto-detection ──────────────────────────────────

TEST(ClusterRouterE2ETest, HealthCheckMarksUnhealthyAndRecovers) {
    ClusterRouter router;
    router.set_health_check([](const NodeId&) { return false; });  // always fail

    NodeId node{.host = "10.0.0.1", .port = 9302, .node_name = "login-1"};
    router.register_service(ServiceInstance{
        .node = node,
        .service_name = "login",
    });

    // Initially healthy
    EXPECT_TRUE(router.discover("login").has_value());

    // Run health checks enough times to exceed failure_threshold (default 3)
    router.run_health_checks();  // failure #1
    router.run_health_checks();  // failure #2
    router.run_health_checks();  // failure #3 → should flip to unhealthy
    EXPECT_FALSE(router.discover("login").has_value());

    // Now make health check pass
    router.set_health_check([](const NodeId&) { return true; });  // always pass
    router.run_health_checks();  // success #1
    router.run_health_checks();  // success #2 → should recover
    EXPECT_TRUE(router.discover("login").has_value());
}

TEST(ClusterRouterE2ETest, HealthCheckPerNodeIndependence) {
    ClusterRouter router;
    bool node1_healthy = true;
    bool node2_healthy = true;

    router.set_health_check([&](const NodeId& n) -> bool {
        if (n.node_name == "room-1") return node1_healthy;
        if (n.node_name == "room-2") return node2_healthy;
        return true;
    });

    NodeId n1{.host = "10.0.0.1", .port = 9402, .node_name = "room-1"};
    NodeId n2{.host = "10.0.0.2", .port = 9402, .node_name = "room-2"};
    router.register_service(ServiceInstance{.node = n1, .service_name = "room"});
    router.register_service(ServiceInstance{.node = n2, .service_name = "room"});

    // Both healthy
    auto d = router.discover("room");
    ASSERT_TRUE(d.has_value());

    // Take room-1 down
    node1_healthy = false;
    for (int i = 0; i < 3; ++i) router.run_health_checks();

    // Should still find room-2
    d = router.discover("room");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->node.node_name, "room-2");

    // Take room-2 down too
    node2_healthy = false;
    for (int i = 0; i < 3; ++i) router.run_health_checks();

    // No healthy rooms
    EXPECT_FALSE(router.discover("room").has_value());

    // Recover room-1
    node1_healthy = true;
    for (int i = 0; i < 2; ++i) router.run_health_checks();
    EXPECT_TRUE(router.discover("room").has_value());
}

// ─── Test 4: Gateway service bridge integration with ClusterRouter ─────────

TEST(ClusterRouterE2ETest, GatewayBridgeUsesClusterRouterWhenSet) {
    auto bridge = std::make_unique<GatewayServiceBridge>();
    auto router = std::make_shared<ClusterRouter>();

    // Register a login backend in the cluster
    router->register_service(ServiceInstance{
        .node = {.host = "192.168.1.100", .port = 9302, .node_name = "login-prod-1"},
        .service_name = "login",
    });

    // Set cluster router on the bridge
    bridge->set_cluster_router(router);

    // Bridge should discover from cluster router
    auto resolved = bridge->resolve_backend(v2::service::ServiceId::kLogin);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved->from_cluster);
    EXPECT_EQ(resolved->config.host, "192.168.1.100");
    EXPECT_EQ(resolved->config.port, 9302);
}

TEST(ClusterRouterE2ETest, GatewayBridgeFallsBackToStaticWhenClusterRouterReturnsNone) {
    auto bridge = std::make_unique<GatewayServiceBridge>(
        GatewayServiceBridge::BackendConfig{.host = "127.0.0.1", .port = 9302},
        std::nullopt, std::nullopt, std::nullopt, std::nullopt);

    auto router = std::make_shared<ClusterRouter>();
    router->register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9302, .node_name = "login-1"},
        .service_name = "login",
    });

    // Mark the cluster instance unhealthy, so discover() returns nullopt
    router->mark_unhealthy("login", NodeId{.host = "10.0.0.1", .port = 9302, .node_name = "login-1"});
    bridge->set_cluster_router(router);

    // Bridge should fall back to static config since cluster router returns none
    auto resolved = bridge->resolve_backend(v2::service::ServiceId::kLogin);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(resolved->from_cluster);
    EXPECT_EQ(resolved->config.host, "127.0.0.1");
    EXPECT_EQ(resolved->config.port, 9302);
}

TEST(ClusterRouterE2ETest, GatewayBridgeReturnsNulloptWhenBothClusterAndStaticEmpty) {
    auto bridge = std::make_unique<GatewayServiceBridge>();
    auto router = std::make_shared<ClusterRouter>();

    // Set cluster router but register nothing
    bridge->set_cluster_router(router);

    // No login registered in cluster and no static config
    auto resolved = bridge->resolve_backend(v2::service::ServiceId::kLogin);
    EXPECT_FALSE(resolved.has_value());
}

TEST(ClusterRouterE2ETest, GatewayBridgeClusterRouterShardAffinity) {
    auto router = std::make_shared<ClusterRouter>();

    // Register 3 room instances with different node names
    router->register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9402, .node_name = "room-alpha"},
        .service_name = "room",
    });
    router->register_service(ServiceInstance{
        .node = {.host = "10.0.0.2", .port = 9402, .node_name = "room-beta"},
        .service_name = "room",
    });
    router->register_service(ServiceInstance{
        .node = {.host = "10.0.0.3", .port = 9402, .node_name = "room-gamma"},
        .service_name = "room",
    });

    // Without a ShardRouter, resolve_backend uses round-robin via discover()
    auto bridge = std::make_unique<GatewayServiceBridge>();
    bridge->set_cluster_router(router);
    bridge->update_backend_config(v2::service::ServiceId::kRoom,
        GatewayServiceBridge::BackendConfig{.host = "127.0.0.1", .port = 9402});

    auto r1 = bridge->resolve_backend(v2::service::ServiceId::kRoom);
    ASSERT_TRUE(r1.has_value());
    EXPECT_TRUE(r1->from_cluster);

    auto r2 = bridge->resolve_backend(v2::service::ServiceId::kRoom);
    ASSERT_TRUE(r2.has_value());
    EXPECT_TRUE(r2->from_cluster);

    // Round-robin: they should point to different nodes
    EXPECT_NE(r1->config.host, r2->config.host);
}

// ─── Test 5: Multi-service discovery ──────────────────────────────────────

TEST(ClusterRouterE2ETest, MultipleServiceTypesDiscoveredIndependently) {
    ClusterRouter router;

    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9302, .node_name = "login-1"},
        .service_name = "login",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.2", .port = 9402, .node_name = "room-1"},
        .service_name = "room",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.3", .port = 9502, .node_name = "battle-1"},
        .service_name = "battle",
    });

    EXPECT_EQ(router.total_services(), 3U);
    EXPECT_EQ(router.healthy_count("login"), 1U);
    EXPECT_EQ(router.healthy_count("room"), 1U);
    EXPECT_EQ(router.healthy_count("battle"), 1U);
    EXPECT_EQ(router.healthy_count("match"), 0U);
    EXPECT_EQ(router.healthy_count("leaderboard"), 0U);

    // Mark room unhealthy — only room count drops
    router.mark_unhealthy("room",
        NodeId{.host = "10.0.0.2", .port = 9402, .node_name = "room-1"});
    EXPECT_EQ(router.healthy_count("login"), 1U);
    EXPECT_EQ(router.healthy_count("room"), 0U);
    EXPECT_EQ(router.healthy_count("battle"), 1U);
}

// ─── Test 6: discover_all returns all healthy instances ───────────────────

TEST(ClusterRouterE2ETest, DiscoverAllReturnsOnlyHealthy) {
    ClusterRouter router;

    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.1", .port = 9302, .node_name = "login-1"},
        .service_name = "login",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.2", .port = 9302, .node_name = "login-2"},
        .service_name = "login",
    });
    router.register_service(ServiceInstance{
        .node = {.host = "10.0.0.3", .port = 9302, .node_name = "login-3"},
        .service_name = "login",
    });

    auto all = router.discover_all("login");
    EXPECT_EQ(all.size(), 3U);

    // Mark login-2 as unhealthy
    router.mark_unhealthy("login",
        NodeId{.host = "10.0.0.2", .port = 9302, .node_name = "login-2"});

    all = router.discover_all("login");
    ASSERT_EQ(all.size(), 2U);
    EXPECT_NE(all[0].node.node_name, "login-2");
    EXPECT_NE(all[1].node.node_name, "login-2");
}
