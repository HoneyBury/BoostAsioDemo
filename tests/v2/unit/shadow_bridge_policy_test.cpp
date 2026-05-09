#include "app/config.h"
#include "app/logging.h"
#include "net/protocol.h"
#include "v2/gateway/gateway_server_bridge.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

TEST(V2ShadowBridgePolicyTest, MirrorsConfiguredProtocolDomainsOnly) {
    v2::gateway::GatewayServerShadowBridge::MirrorPolicy policy;
    policy.login = true;
    policy.room = false;
    policy.battle = true;
    policy.echo = false;

    v2::gateway::GatewayServerShadowBridge bridge(policy, false);

    EXPECT_TRUE(bridge.should_forward(net::protocol::kLoginRequest));
    EXPECT_FALSE(bridge.should_forward(net::protocol::kRoomCreateRequest));
    EXPECT_FALSE(bridge.should_forward(net::protocol::kRoomJoinRequest));
    EXPECT_FALSE(bridge.should_forward(net::protocol::kRoomReadyRequest));
    EXPECT_TRUE(bridge.should_forward(net::protocol::kBattleStartRequest));
    EXPECT_TRUE(bridge.should_forward(net::protocol::kBattleInputRequest));
    EXPECT_FALSE(bridge.should_forward(net::protocol::kEchoRequest));
    EXPECT_FALSE(bridge.should_forward(net::protocol::kHeartbeatRequest));
}

TEST(V2ShadowBridgePolicyTest, BuildsMirrorPolicyFromGatewayConfig) {
    app::config::GatewayAppConfig config;
    config.v2_shadow_bridge_login = false;
    config.v2_shadow_bridge_room = true;
    config.v2_shadow_bridge_battle = false;
    config.v2_shadow_bridge_echo = true;

    const auto policy = v2::gateway::make_shadow_bridge_policy(config);
    EXPECT_FALSE(policy.login);
    EXPECT_TRUE(policy.room);
    EXPECT_FALSE(policy.battle);
    EXPECT_TRUE(policy.echo);
}

TEST(V2ShadowBridgePolicyTest, LoadedGatewayConfigDrivesMirrorPolicy) {
    app::logging::init("project_tests");

    const auto path = std::filesystem::temp_directory_path() / "v2_shadow_bridge_policy.json";
    {
        std::ofstream output(path);
        output << "{\n";
        output << "  \"gateway\": {\n";
        output << "    \"v2_shadow_bridge_enabled\": true,\n";
        output << "    \"v2_shadow_bridge_login\": false,\n";
        output << "    \"v2_shadow_bridge_room\": true,\n";
        output << "    \"v2_shadow_bridge_battle\": false,\n";
        output << "    \"v2_shadow_bridge_echo\": true\n";
        output << "  }\n";
        output << "}\n";
    }

    const auto config = app::config::load_gateway_config(path);
    const auto policy = v2::gateway::make_shadow_bridge_policy(config);
    EXPECT_FALSE(policy.login);
    EXPECT_TRUE(policy.room);
    EXPECT_FALSE(policy.battle);
    EXPECT_TRUE(policy.echo);

    std::filesystem::remove(path);
}
