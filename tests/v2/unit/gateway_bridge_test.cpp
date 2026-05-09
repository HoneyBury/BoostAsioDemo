#include <gtest/gtest.h>

#include <memory>

#include "net/protocol.h"
#include "v2/gateway/session_adapter.h"

TEST(V2GatewayBridgeTest, EchoRequestReturnsEchoResponse) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 7;
    envelope.protocol_message_id = net::protocol::kEchoRequest;
    envelope.request_id = 99;
    envelope.body = "hello-gateway";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kEchoResponse);
    EXPECT_EQ(writes.front().envelope.request_id, 99U);
    EXPECT_EQ(writes.front().envelope.error_code,
              static_cast<std::int32_t>(net::protocol::ErrorCode::kOk));
    EXPECT_EQ(writes.front().envelope.body, "hello-gateway");
}

TEST(V2GatewayBridgeTest, LoginRequestReturnsLoginResponse) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 8;
    envelope.protocol_message_id = net::protocol::kLoginRequest;
    envelope.request_id = 100;
    envelope.body = "player_01|token:player_01|PlayerOne";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kLoginResponse);
    EXPECT_EQ(writes.front().envelope.body, "login_ok:player_01");
}

TEST(V2GatewayBridgeTest, LoginRequestRejectsEmptyUserId) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 8;
    envelope.protocol_message_id = net::protocol::kLoginRequest;
    envelope.request_id = 101;
    envelope.body = "|token:player_01";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kErrorResponse);
    EXPECT_EQ(writes.front().envelope.error_code,
              static_cast<std::int32_t>(net::protocol::ErrorCode::kInvalidUserId));
}

TEST(V2GatewayBridgeTest, NonWhitelistedMessageIsRejected) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 9;
    envelope.protocol_message_id = net::protocol::kRoomJoinRequest;
    envelope.request_id = 101;
    envelope.body = "room_alpha";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kErrorResponse);
    EXPECT_EQ(writes.front().envelope.error_code,
              static_cast<std::int32_t>(net::protocol::ErrorCode::kAuthRequired));
    EXPECT_EQ(writes.front().envelope.body,
              net::protocol::to_string(net::protocol::ErrorCode::kAuthRequired));
}

TEST(V2GatewayBridgeTest, RateLimitPolicyCanRejectRequest) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(
            adapter,
            nullptr,
            [](const v2::gateway::ClientEnvelope&) { return false; }));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 10;
    envelope.protocol_message_id = net::protocol::kEchoRequest;
    envelope.request_id = 102;
    envelope.body = "rate_limited";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kErrorResponse);
    EXPECT_EQ(writes.front().envelope.error_code,
              static_cast<std::int32_t>(net::protocol::ErrorCode::kRateLimited));
    EXPECT_EQ(writes.front().envelope.body,
              net::protocol::to_string(net::protocol::ErrorCode::kRateLimited));
}

TEST(V2GatewayBridgeTest, EchoWithEmptyBodyReturnsEmptyEcho) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 11;
    envelope.protocol_message_id = net::protocol::kEchoRequest;
    envelope.request_id = 200;
    envelope.body.clear();

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kEchoResponse);
    EXPECT_EQ(writes.front().envelope.body, "");
}

TEST(V2GatewayBridgeTest, EchoRequestPreservesRequestId) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    v2::gateway::ClientEnvelope envelope;
    envelope.session_id = 12;
    envelope.protocol_message_id = net::protocol::kEchoRequest;
    envelope.request_id = 0;
    envelope.body = "zero-reqid";

    const auto writes = adapter.handle_incoming(envelope);
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.request_id, 0U);
    EXPECT_EQ(writes.front().envelope.body, "zero-reqid");
}

TEST(V2GatewayBridgeTest, MultipleLoginReturnsSeparateResponses) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    const auto writes1 = adapter.handle_incoming(v2::gateway::ClientEnvelope{
        .session_id = 20,
        .protocol_message_id = net::protocol::kLoginRequest,
        .request_id = 1,
        .body = "user_a|token:a|Alice",
    });
    ASSERT_EQ(writes1.size(), 1U);
    EXPECT_EQ(writes1.front().envelope.body, "login_ok:user_a");

    const auto writes2 = adapter.handle_incoming(v2::gateway::ClientEnvelope{
        .session_id = 21,
        .protocol_message_id = net::protocol::kLoginRequest,
        .request_id = 2,
        .body = "user_b|token:b|Bob",
    });
    ASSERT_EQ(writes2.size(), 1U);
    EXPECT_EQ(writes2.front().envelope.body, "login_ok:user_b");
}

TEST(V2GatewayBridgeTest, VerifyLoginUsersCanEcho) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    adapter.handle_incoming(v2::gateway::ClientEnvelope{
        .session_id = 30,
        .protocol_message_id = net::protocol::kLoginRequest,
        .request_id = 1,
        .body = "user_echo|token:e|EchoUser",
    });

    const auto writes = adapter.handle_incoming(v2::gateway::ClientEnvelope{
        .session_id = 30,
        .protocol_message_id = net::protocol::kEchoRequest,
        .request_id = 2,
        .body = "echo_after_login",
    });
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kEchoResponse);
    EXPECT_EQ(writes.front().envelope.body, "echo_after_login");
}

TEST(V2GatewayBridgeTest, HeartbeatReturnsResponse) {
    v2::runtime::ActorSystem actor_system;
    v2::gateway::SessionAdapter adapter(actor_system);
    auto gateway_actor = actor_system.create_actor(
        std::make_unique<v2::gateway::GatewayActor>(adapter));
    adapter.bind_gateway(gateway_actor);

    const auto writes = adapter.handle_incoming(v2::gateway::ClientEnvelope{
        .session_id = 40,
        .protocol_message_id = net::protocol::kHeartbeatRequest,
        .request_id = 0,
        .body = {},
    });
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes.front().envelope.protocol_message_id, net::protocol::kHeartbeatResponse);
}
