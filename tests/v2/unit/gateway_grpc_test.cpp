// gateway_grpc_test.cpp — Unit tests for the gRPC Gateway server
//
// Tests the GatewayGrpcServer end-to-end by starting an embedded gRPC
// server on an OS-assigned port and issuing RPCs through a client stub.
//
// Requires BOOST_BUILD_GRPC (gRPC libraries + protoc-generated stubs).

#ifdef BOOST_BUILD_GRPC

#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "grpc_server.h"
#include "gateway_grpc_server.h"
#include "gateway.grpc.pb.h"

namespace {

using boost::gateway::v3::Gateway;

struct GatewayGrpcTest : ::testing::Test {
    static std::string last_logout_user;
    static std::string last_logout_session;
    static int logout_call_count;

    static void SetUpTestSuite() {
        last_logout_user.clear();
        last_logout_session.clear();
        logout_call_count = 0;
    }

    void SetUp() override {
        last_logout_user.clear();
        last_logout_session.clear();
        logout_call_count = 0;

        server = std::make_unique<v2::grpc::GatewayGrpcServer>(
            0,
            [](const std::string& user_id,
               const std::string& token,
               std::string& error) -> bool {
                if (user_id == "valid_user" && token == "valid_token")
                    return true;
                if (user_id == "empty_token_user" && !token.empty())
                    return true;
                if (user_id == "auth_ok_user")
                    return true;
                error = "invalid credentials";
                return false;
            },
            [](const std::string& user_id,
               const std::string& session_id) {
                last_logout_user = user_id;
                last_logout_session = session_id;
                ++logout_call_count;
            });

        ASSERT_TRUE(server->start());

        const auto target =
            "127.0.0.1:" + std::to_string(server->port());
        auto channel = grpc::CreateChannel(
            target, grpc::InsecureChannelCredentials());

        // Start CQ polling thread
        running_ = true;
        cq_thread_ = std::thread([this] {
            auto* cq = server->completion_queue();
            if (!cq) return;
            server->seed_completion_queue();
            void* tag;
            bool ok;
            while (running_ && cq->Next(&tag, &ok)) {
                auto* call = static_cast<v2::grpc::GatewayGrpcServer::CallData*>(tag);
                if (call) call->proceed(ok);
            }
        });

        stub = Gateway::NewStub(channel);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        running_ = false;
        stub.reset();
        if (server) {
            server->shutdown();
        }
        if (cq_thread_.joinable())
            cq_thread_.join();
        server.reset();
    }

    boost::gateway::v3::LoginResponse do_login(
        const std::string& user_id,
        const std::string& token,
        const std::string& display_name = "") {
        grpc::ClientContext ctx;
        boost::gateway::v3::LoginRequest req;
        req.set_user_id(user_id);
        req.set_token(token);
        if (!display_name.empty())
            req.set_display_name(display_name);

        boost::gateway::v3::LoginResponse resp;
        auto status = stub->RequestLogin(&ctx, req, &resp);
        EXPECT_TRUE(status.ok()) << "Login RPC failed: "
                                  << status.error_message();
        return resp;
    }

    std::unique_ptr<v2::grpc::GatewayGrpcServer> server;
    std::unique_ptr<Gateway::Stub> stub;
    std::thread cq_thread_;
    std::atomic<bool> running_{false};
};

std::string GatewayGrpcTest::last_logout_user;
std::string GatewayGrpcTest::last_logout_session;
int GatewayGrpcTest::logout_call_count;

// ─── Login tests ──────────────────────────────────────────────────────

TEST_F(GatewayGrpcTest, LoginSuccess) {
    auto resp = do_login("valid_user", "valid_token", "Valid User");
    EXPECT_EQ(resp.error_code(), 0);
    EXPECT_EQ(resp.user_id(), "valid_user");
    EXPECT_EQ(resp.display_name(), "Valid User");
    EXPECT_EQ(resp.role(), "player");
    EXPECT_TRUE(resp.error_message().empty());
}

TEST_F(GatewayGrpcTest, LoginAuthFailure) {
    auto resp = do_login("valid_user", "wrong_token", "Attacker");
    EXPECT_NE(resp.error_code(), 0);
    EXPECT_EQ(resp.user_id(), "valid_user");
    EXPECT_FALSE(resp.error_message().empty());
}

TEST_F(GatewayGrpcTest, LoginUnknownUser) {
    auto resp = do_login("nonexistent", "some_token");
    EXPECT_NE(resp.error_code(), 0);
    EXPECT_FALSE(resp.error_message().empty());
}

TEST_F(GatewayGrpcTest, LoginEmptyUserId) {
    auto resp = do_login("", "valid_token");
    EXPECT_NE(resp.error_code(), 0);
    EXPECT_FALSE(resp.error_message().empty());
}

TEST_F(GatewayGrpcTest, LoginEmptyToken) {
    auto resp = do_login("valid_user", "");
    EXPECT_NE(resp.error_code(), 0);
    EXPECT_FALSE(resp.error_message().empty());
}

TEST_F(GatewayGrpcTest, LoginMultipleSessionsAccumulate) {
    do_login("valid_user", "valid_token");
    do_login("auth_ok_user", "irrelevant");
    EXPECT_GE(server->active_sessions(), 2U);
}

// ─── Logout tests ─────────────────────────────────────────────────────

TEST_F(GatewayGrpcTest, LogoutSuccess) {
    do_login("valid_user", "valid_token");

    grpc::ClientContext ctx;
    boost::gateway::v3::LogoutRequest lreq;
    lreq.set_user_id("valid_user");
    lreq.set_session_id("session_001");

    boost::gateway::v3::LogoutResponse lresp;
    auto status = stub->RequestLogout(&ctx, lreq, &lresp);

    ASSERT_TRUE(status.ok());
    EXPECT_TRUE(lresp.success());
    EXPECT_EQ(last_logout_user, "valid_user");
    EXPECT_EQ(last_logout_session, "session_001");
    EXPECT_EQ(logout_call_count, 1);
}

TEST_F(GatewayGrpcTest, LogoutDecrementsActiveSessions) {
    do_login("valid_user", "valid_token");
    do_login("auth_ok_user", "irrelevant");

    {
        grpc::ClientContext ctx;
        boost::gateway::v3::LogoutRequest lreq;
        lreq.set_user_id("valid_user");
        lreq.set_session_id("s1");

        boost::gateway::v3::LogoutResponse lresp;
        ASSERT_TRUE(stub->RequestLogout(&ctx, lreq, &lresp).ok());
    }

    EXPECT_EQ(server->active_sessions(), 1U);
}

// ─── Health tests ─────────────────────────────────────────────────────

TEST_F(GatewayGrpcTest, HealthReturnsServingStatus) {
    grpc::ClientContext ctx;
    boost::gateway::v3::HealthCheckRequest req;
    boost::gateway::v3::HealthCheckResponse resp;
    auto status = stub->Health(&ctx, req, &resp);

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(resp.status(), "SERVING");
}

TEST_F(GatewayGrpcTest, ActiveSessionsAfterLogin) {
    do_login("valid_user", "valid_token");
    do_login("auth_ok_user", "irrelevant");

    EXPECT_EQ(server->active_sessions(), 2U);
}

// ─── Concurrent logins ────────────────────────────────────────────────

TEST_F(GatewayGrpcTest, ConcurrentLogins) {
    constexpr int kNumClients = 10;
    std::atomic<int> success_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumClients; ++i) {
        threads.emplace_back([this, i, &success_count] {
            auto channel = grpc::CreateChannel(
                "127.0.0.1:" + std::to_string(server->port()),
                grpc::InsecureChannelCredentials());
            auto local_stub = Gateway::NewStub(channel);

            grpc::ClientContext ctx;
            boost::gateway::v3::LoginRequest req;
            req.set_user_id("valid_user");
            req.set_token("valid_token");
            req.set_display_name("User" + std::to_string(i));

            boost::gateway::v3::LoginResponse resp;
            auto status = local_stub->RequestLogin(&ctx, req, &resp);
            if (status.ok() && resp.error_code() == 0)
                success_count.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (auto& t : threads)
        t.join();

    EXPECT_EQ(success_count.load(), kNumClients);
}

}  // anonymous namespace

#else  // !BOOST_BUILD_GRPC

#include <gtest/gtest.h>

TEST(GatewayGrpcTest, Skipped) {
    GTEST_SKIP() << "gRPC not enabled (BOOST_BUILD_GRPC not set)";
}

#endif  // BOOST_BUILD_GRPC
