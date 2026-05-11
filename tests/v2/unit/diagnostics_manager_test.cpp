#include "v2/diagnostics/diagnostics_manager.h"
#include "v2/gateway/backend_metrics.h"
#include "v2/gateway/gateway_server_bridge.h"
#include "v2/service/service_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace v2::diagnostics {
namespace {

// ── Test 1: EmptyManagerReturnsDefaultSnapshot ─────────

TEST(DiagnosticsManagerTest, EmptyManagerReturnsDefaultSnapshot) {
    DiagnosticsManager mgr;
    auto snap = mgr.collect();

    EXPECT_TRUE(snap.summary.overall_healthy);
    EXPECT_EQ(snap.summary.total_active_sessions, 0u);
    EXPECT_EQ(snap.summary.total_accepted_sessions, 0u);
    EXPECT_EQ(snap.summary.io_core_count, 0u);
    EXPECT_EQ(snap.summary.registered_backend_count, 0u);
    EXPECT_EQ(snap.summary.healthy_backend_count, 0u);
    EXPECT_TRUE(snap.backends.empty());
    EXPECT_TRUE(snap.io_cores.empty());
    EXPECT_FALSE(snap.shadow_bridge.enabled);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.mirrored_packets, 0u);
}

// ── Test 2: BackendMetricsFlowThrough ──────────────────

TEST(DiagnosticsManagerTest, BackendMetricsFlowThrough) {
    auto metrics = std::make_shared<v2::gateway::BackendMetrics>();

    metrics->record_request(v2::service::ServiceId::kLogin);
    metrics->record_success(v2::service::ServiceId::kLogin);
    metrics->record_request(v2::service::ServiceId::kRoom);
    metrics->record_timeout(v2::service::ServiceId::kRoom);
    metrics->record_unavailable(v2::service::ServiceId::kBattle);
    metrics->record_error(v2::service::ServiceId::kBattle);

    DiagnosticsManager mgr;
    mgr.set_backend_metrics(metrics);

    auto snap = mgr.collect();

    ASSERT_EQ(snap.backends.size(), 3u);

    // Login
    auto login_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "login"; });
    ASSERT_NE(login_it, snap.backends.end());
    EXPECT_EQ(login_it->metrics.total_requests, 1u);
    EXPECT_EQ(login_it->metrics.total_successes, 1u);
    EXPECT_EQ(login_it->metrics.total_timeouts, 0u);

    // Room
    auto room_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "room"; });
    ASSERT_NE(room_it, snap.backends.end());
    EXPECT_EQ(room_it->metrics.total_requests, 1u);
    EXPECT_EQ(room_it->metrics.total_timeouts, 1u);

    // Battle
    auto battle_it = std::find_if(
        snap.backends.begin(), snap.backends.end(),
        [](const BackendEntry& e) { return e.service_name == "battle"; });
    ASSERT_NE(battle_it, snap.backends.end());
    EXPECT_EQ(battle_it->metrics.total_unavailable, 1u);
    EXPECT_EQ(battle_it->metrics.total_errors, 1u);

    EXPECT_EQ(snap.summary.registered_backend_count, 3u);
}

// ── Test 3: ServiceRegistryFlowThrough ─────────────────

TEST(DiagnosticsManagerTest, ServiceRegistryFlowThrough) {
    auto metrics = std::make_shared<v2::gateway::BackendMetrics>();
    auto registry = std::make_shared<v2::service::ServiceRegistry>();

    // Register backends and trigger heartbeats so they are healthy
    registry->register_instance(v2::service::ServiceId::kLogin, "192.168.1.10",
                                8081);
    registry->heartbeat(v2::service::ServiceId::kLogin, "192.168.1.10", 8081);

    registry->register_instance(v2::service::ServiceId::kRoom, "192.168.1.20",
                                8082);
    registry->heartbeat(v2::service::ServiceId::kRoom, "192.168.1.20", 8082);

    // Mark Battle as unhealthy
    registry->register_instance(v2::service::ServiceId::kBattle, "192.168.1.30",
                                8083);
    registry->mark_unhealthy(v2::service::ServiceId::kBattle, "192.168.1.30",
                             8083);

    // Ensure Login has a metric entry so it appears in backends
    metrics->record_request(v2::service::ServiceId::kLogin);
    metrics->record_request(v2::service::ServiceId::kRoom);
    metrics->record_request(v2::service::ServiceId::kBattle);

    DiagnosticsManager mgr;
    mgr.set_backend_metrics(metrics);
    mgr.set_service_registry(registry);

    auto snap = mgr.collect();

    ASSERT_EQ(snap.backends.size(), 3u);

    // Login should be healthy with 1 healthy instance
    auto login_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "login"; });
    ASSERT_NE(login_it, snap.backends.end());
    EXPECT_TRUE(login_it->healthy);
    EXPECT_EQ(login_it->healthy_instances, 1u);
    EXPECT_EQ(login_it->unhealthy_instances, 0u);

    // Room should be healthy
    auto room_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "room"; });
    ASSERT_NE(room_it, snap.backends.end());
    EXPECT_TRUE(room_it->healthy);
    EXPECT_EQ(room_it->healthy_instances, 1u);

    // Battle should be unhealthy
    auto battle_it = std::find_if(
        snap.backends.begin(), snap.backends.end(),
        [](const BackendEntry& e) { return e.service_name == "battle"; });
    ASSERT_NE(battle_it, snap.backends.end());
    EXPECT_FALSE(battle_it->healthy);
    EXPECT_EQ(battle_it->healthy_instances, 0u);
    EXPECT_EQ(battle_it->unhealthy_instances, 1u);

    EXPECT_EQ(snap.summary.healthy_backend_count, 2u);
}

// ── Test 4: IoCoreProviderFlowThrough ──────────────────

TEST(DiagnosticsManagerTest, IoCoreProviderFlowThrough) {
    DiagnosticsManager mgr;
    mgr.set_io_core_provider([]() -> std::vector<IoCoreEntry> {
        return {
            IoCoreEntry{.core_id = 0,
                        .active_sessions = 10,
                        .accepted_sessions = 100,
                        .outbound_dispatches = 500},
            IoCoreEntry{.core_id = 1,
                        .active_sessions = 5,
                        .accepted_sessions = 50,
                        .outbound_dispatches = 300},
        };
    });

    auto snap = mgr.collect();

    ASSERT_EQ(snap.io_cores.size(), 2u);
    EXPECT_EQ(snap.io_cores[0].core_id, 0u);
    EXPECT_EQ(snap.io_cores[0].active_sessions, 10u);
    EXPECT_EQ(snap.io_cores[0].accepted_sessions, 100u);
    EXPECT_EQ(snap.io_cores[0].outbound_dispatches, 500u);

    EXPECT_EQ(snap.io_cores[1].core_id, 1u);
    EXPECT_EQ(snap.io_cores[1].active_sessions, 5u);

    EXPECT_EQ(snap.summary.io_core_count, 2u);
    EXPECT_EQ(snap.summary.total_active_sessions, 15u);
    EXPECT_EQ(snap.summary.total_accepted_sessions, 150u);
}

// ── Test 5: ShadowBridgeProviderFlowThrough ────────────

TEST(DiagnosticsManagerTest, ShadowBridgeProviderFlowThrough) {
    DiagnosticsManager mgr;
    mgr.set_shadow_bridge_provider([]() -> ShadowBridgeEntry {
        ShadowBridgeEntry entry;
        entry.enabled = true;
        entry.emit_responses = true;
        entry.dispatch_stats.mirrored_packets = 42;
        entry.dispatch_stats.emitted_writes = 10;
        entry.dispatch_stats.scheduled_writes = 7;
        entry.dispatch_stats.inline_writes = 3;
        entry.tracked_sessions = 100;
        entry.active_sessions = 88;
        return entry;
    });

    auto snap = mgr.collect();

    EXPECT_TRUE(snap.shadow_bridge.enabled);
    EXPECT_TRUE(snap.shadow_bridge.emit_responses);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.mirrored_packets, 42u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.emitted_writes, 10u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.scheduled_writes, 7u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.inline_writes, 3u);
    EXPECT_EQ(snap.shadow_bridge.tracked_sessions, 100u);
    EXPECT_EQ(snap.shadow_bridge.active_sessions, 88u);
}

// ── Test 6: ToJsonIsValidJson ───────────────────────────

TEST(DiagnosticsManagerTest, ToJsonIsValidJson) {
    DiagnosticsSnapshot snap;

    // Populate with some data
    snap.summary.overall_healthy = true;
    snap.summary.total_active_sessions = 15;
    snap.summary.total_accepted_sessions = 150;
    snap.summary.io_core_count = 2;
    snap.summary.registered_backend_count = 1;
    snap.summary.healthy_backend_count = 1;

    snap.backends.push_back(BackendEntry{
        .service_name = "login",
        .healthy = true,
        .healthy_instances = 2,
        .unhealthy_instances = 0,
        .metrics =
            v2::gateway::BackendMetricsSnapshot{
                .total_requests = 10,
                .total_successes = 8,
                .total_timeouts = 1,
                .total_unavailable = 0,
                .total_errors = 1,
            },
    });

    snap.io_cores.push_back(IoCoreEntry{
        .core_id = 0,
        .active_sessions = 10,
        .accepted_sessions = 100,
        .outbound_dispatches = 500,
    });

    snap.shadow_bridge.enabled = true;
    snap.shadow_bridge.dispatch_stats.mirrored_packets = 42;

    DiagnosticsManager mgr;
    auto json = mgr.to_json(snap);

    // Validate basic JSON structure
    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '\n');

    // Contains key fields
    EXPECT_NE(json.find("\"summary\""), std::string::npos);
    EXPECT_NE(json.find("\"backends\""), std::string::npos);
    EXPECT_NE(json.find("\"io_cores\""), std::string::npos);
    EXPECT_NE(json.find("\"shadow_bridge\""), std::string::npos);
    EXPECT_NE(json.find("\"overall_healthy\""), std::string::npos);
    EXPECT_NE(json.find("\"total_active_sessions\""), std::string::npos);
    EXPECT_NE(json.find("\"service_name\""), std::string::npos);
    EXPECT_NE(json.find("\"metrics\""), std::string::npos);
    EXPECT_NE(json.find("\"dispatch_stats\""), std::string::npos);
    EXPECT_NE(json.find("\"login\""), std::string::npos);
    EXPECT_NE(json.find("\"mirrored_packets\""), std::string::npos);

    // Verify the text output is also non-empty
    auto text = mgr.to_text(snap);
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("System Summary"), std::string::npos);
    EXPECT_NE(text.find("Backends"), std::string::npos);
    EXPECT_NE(text.find("IO Cores"), std::string::npos);
    EXPECT_NE(text.find("Shadow Bridge"), std::string::npos);
}

// ── Test 7: OverallHealthyFalseWhenBackendUnhealthy ───

TEST(DiagnosticsManagerTest, OverallHealthyFalseWhenBackendUnhealthy) {
    auto metrics = std::make_shared<v2::gateway::BackendMetrics>();
    auto registry = std::make_shared<v2::service::ServiceRegistry>();

    // Register a login backend and keep it healthy
    registry->register_instance(v2::service::ServiceId::kLogin, "192.168.1.10",
                                8081);
    registry->heartbeat(v2::service::ServiceId::kLogin, "192.168.1.10", 8081);

    // Register and mark a room backend as unhealthy
    registry->register_instance(v2::service::ServiceId::kRoom, "192.168.1.20",
                                8082);
    registry->mark_unhealthy(v2::service::ServiceId::kRoom, "192.168.1.20",
                             8082);

    // Ensure both have metrics
    metrics->record_request(v2::service::ServiceId::kLogin);
    metrics->record_request(v2::service::ServiceId::kRoom);

    DiagnosticsManager mgr;
    mgr.set_backend_metrics(metrics);
    mgr.set_service_registry(registry);

    auto snap = mgr.collect();

    // Room is unhealthy, so overall_healthy should be false
    EXPECT_FALSE(snap.summary.overall_healthy);

    // Verify individual backend health
    auto login_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "login"; });
    ASSERT_NE(login_it, snap.backends.end());
    EXPECT_TRUE(login_it->healthy);

    auto room_it =
        std::find_if(snap.backends.begin(), snap.backends.end(),
                     [](const BackendEntry& e) { return e.service_name == "room"; });
    ASSERT_NE(room_it, snap.backends.end());
    EXPECT_FALSE(room_it->healthy);
}

// ── Test 8: WireFromShadowBridge ───────────────────────

TEST(DiagnosticsManagerTest, WireFromShadowBridge) {
    DiagnosticsManager mgr;

    v2::gateway::GatewayServerShadowBridge::Diagnostics diag;
    diag.emit_responses = true;
    diag.dispatch_stats.mirrored_packets = 99;
    diag.dispatch_stats.emitted_writes = 50;
    diag.dispatch_stats.scheduled_writes = 30;
    diag.dispatch_stats.inline_writes = 20;
    diag.tracked_sessions = 200;
    diag.active_sessions = 150;

    mgr.wire_from_shadow_bridge(diag);

    auto snap = mgr.collect();

    EXPECT_TRUE(snap.shadow_bridge.enabled);
    EXPECT_TRUE(snap.shadow_bridge.emit_responses);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.mirrored_packets, 99u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.emitted_writes, 50u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.scheduled_writes, 30u);
    EXPECT_EQ(snap.shadow_bridge.dispatch_stats.inline_writes, 20u);
    EXPECT_EQ(snap.shadow_bridge.tracked_sessions, 200u);
    EXPECT_EQ(snap.shadow_bridge.active_sessions, 150u);
}

// ── Test 9: MultipleCollectReturnsConsistentData ───────

TEST(DiagnosticsManagerTest, MultipleCollectReturnsConsistentData) {
    auto metrics = std::make_shared<v2::gateway::BackendMetrics>();

    metrics->record_request(v2::service::ServiceId::kLogin);
    metrics->record_request(v2::service::ServiceId::kLogin);

    DiagnosticsManager mgr;
    mgr.set_backend_metrics(metrics);

    auto snap1 = mgr.collect();
    auto snap2 = mgr.collect();

    // Both snapshots should contain the same data
    ASSERT_EQ(snap1.backends.size(), 1u);
    ASSERT_EQ(snap2.backends.size(), 1u);
    EXPECT_EQ(snap1.backends[0].metrics.total_requests, 2u);
    EXPECT_EQ(snap2.backends[0].metrics.total_requests, 2u);
}

// ── Test 10: ToJsonNoBackendsIsValid ───────────────────

TEST(DiagnosticsManagerTest, ToJsonNoBackendsIsValid) {
    DiagnosticsSnapshot snap;

    DiagnosticsManager mgr;
    auto json = mgr.to_json(snap);

    EXPECT_EQ(json.front(), '{');
    EXPECT_NE(json.find("\"backends\":["), std::string::npos);
    EXPECT_NE(json.find("\"io_cores\":["), std::string::npos);
}

}  // namespace
}  // namespace v2::diagnostics
