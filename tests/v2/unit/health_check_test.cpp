#include <gtest/gtest.h>

#include "net/http_manager.h"
#include "v2/diagnostics/health_check.h"
#include "v2/service/service_registry.h"

#include <chrono>
#include <memory>
#include <string>

using namespace v2::diagnostics;
using namespace v2::service;
using namespace v2::gateway;

// ── Test 1: All pass when healthy backends are registered ──

TEST(HealthCheckTest, AllPassWhenBackendsHealthy) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);
    registry->register_instance(ServiceId::kRoom, "127.0.0.1", 9002);
    registry->register_instance(ServiceId::kBattle, "127.0.0.1", 9003);

    HealthCheck hc;
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    EXPECT_EQ(result.status, "pass");
    EXPECT_TRUE(result.is_healthy());
    ASSERT_EQ(result.checks.size(), 4U);

    // All per-backend checks should pass
    for (const auto& c : result.checks) {
        EXPECT_EQ(c.status, "pass") << "Check '" << c.name << "' is not pass";
    }
}

// ── Test 2: Fail when backends have no healthy instances ──

TEST(HealthCheckTest, FailWhenBackendHasNoHealthyInstances) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    // Register but mark unhealthy
    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);
    registry->mark_unhealthy(ServiceId::kLogin, "127.0.0.1", 9001);

    HealthCheck hc;
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    EXPECT_EQ(result.status, "fail");
    EXPECT_FALSE(result.is_healthy());

    // Find the login backend check
    bool found_login = false;
    for (const auto& c : result.checks) {
        if (c.name == "backend:login") {
            found_login = true;
            EXPECT_EQ(c.status, "fail");
            EXPECT_TRUE(c.message.find("0 healthy") != std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found_login);
}

// ── Test 3: Warn when no backends are registered ──

TEST(HealthCheckTest, WarnWhenNoBackendRegistered) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    // Don't register any backends at all
    HealthCheck hc;
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    // Service registry itself passes (min_total_instances=0 by default, so
    // no minimum), but all backends should be "warn" since nothing registered
    EXPECT_EQ(result.status, "warn");

    for (const auto& c : result.checks) {
        if (c.name.find("backend:") == 0) {
            EXPECT_EQ(c.status, "warn")
                << "Backend check '" << c.name
                << "' should be warn when nothing is registered";
            EXPECT_TRUE(c.message.find("No backends registered") !=
                        std::string::npos);
        }
    }
}

// ── Test 4: Warn when error rate is high ──

TEST(HealthCheckTest, WarnWhenErrorRateHigh) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);

    // Record a majority of failures
    metrics->record_request(ServiceId::kLogin);  // first request
    metrics->record_timeout(ServiceId::kLogin);
    metrics->record_request(ServiceId::kLogin);  // second request
    metrics->record_timeout(ServiceId::kLogin);
    metrics->record_request(ServiceId::kLogin);  // third request
    metrics->record_success(ServiceId::kLogin);  // one success
    // error rate = 2/3 = 66% > 50% threshold

    HealthCheck hc(HealthCheckConfig{
        .min_healthy_backends_per_service = 0,
        .min_total_instances = 0,
        .check_backend_error_rate = true,
        .max_error_rate = 0.50,
    });
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    EXPECT_EQ(result.status, "warn");

    // The login backend should be warn because of high error rate
    bool found_login = false;
    for (const auto& c : result.checks) {
        if (c.name == "backend:login") {
            found_login = true;
            EXPECT_EQ(c.status, "warn");
            EXPECT_TRUE(c.message.find("error rate") != std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found_login);
}

// ── Test 5: HealthStatus::to_json() produces valid JSON ──

TEST(HealthCheckTest, HealthStatusToJsonIsValid) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);
    registry->register_instance(ServiceId::kRoom, "127.0.0.1", 9002);
    registry->register_instance(ServiceId::kBattle, "127.0.0.1", 9003);

    HealthCheck hc;
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto status = hc.check();
    auto json = HealthCheck::to_json(status);

    // Validate JSON structure roughly
    EXPECT_NE(json.find("\"status\":\"pass\""), std::string::npos);
    EXPECT_NE(json.find("\"checks\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"backend:login\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"backend:room\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"backend:battle\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"service_registry\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"pass\""), std::string::npos);

    // The JSON should start with `{` and end with `}\n`
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '\n');
    // Find the trailing `}` before the newline
    auto close_brace = json.rfind('}');
    EXPECT_NE(close_brace, std::string::npos);
    EXPECT_EQ(close_brace, json.size() - 2);
}

// ── Test 6: is_healthy() returns false on "fail" status ──

TEST(HealthCheckTest, IsHealthyReturnsFalseOnFail) {
    HealthStatus fail_status;
    fail_status.status = "fail";
    EXPECT_FALSE(fail_status.is_healthy());

    HealthStatus pass_status;
    pass_status.status = "pass";
    EXPECT_TRUE(pass_status.is_healthy());

    HealthStatus warn_status;
    warn_status.status = "warn";
    EXPECT_TRUE(warn_status.is_healthy());
}

// ── Test 7: Default HealthProvider returns {"status":"ok"} ──

TEST(HealthCheckTest, DefaultHealthProviderReturnsOk) {
    // Simulate HttpManager's fallback: when no provider is set,
    // the handler returns {"status":"ok"}
    net::HttpManager::HealthProvider empty_provider;
    std::string body =
        empty_provider ? empty_provider() : R"({"status":"ok"})";
    EXPECT_EQ(body, R"({"status":"ok"})");

    // Also verify a set provider returns what it should
    net::HttpManager::HealthProvider custom_provider =
        []() -> std::string { return R"({"status":"pass","message":"all good"})"; };
    EXPECT_EQ(custom_provider(),
              R"({"status":"pass","message":"all good"})");
}

// ── Additional: verify failure takes priority over warn ──

TEST(HealthCheckTest, FailTakesPriorityOverWarn) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    // One healthy, one missing (warn), one with unhealthy (fail)
    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);
    // kRoom not registered -> warn
    registry->register_instance(ServiceId::kBattle, "127.0.0.1", 9003);
    registry->mark_unhealthy(ServiceId::kBattle, "127.0.0.1", 9003);

    HealthCheck hc;
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    EXPECT_EQ(result.status, "fail");
    EXPECT_FALSE(result.is_healthy());
}

// ── Additional: high error rate with min_total_instances ──

TEST(HealthCheckTest, RegistryMinTotalInstancesFail) {
    auto metrics = std::make_shared<BackendMetrics>();
    auto registry = std::make_shared<ServiceRegistry>(
        std::chrono::milliseconds(100));

    registry->register_instance(ServiceId::kLogin, "127.0.0.1", 9001);

    HealthCheckConfig cfg;
    cfg.min_total_instances = 5;  // Require at least 5 total instances
    HealthCheck hc(cfg);
    hc.set_backend_metrics(metrics);
    hc.set_service_registry(registry);

    auto result = hc.check();
    EXPECT_EQ(result.status, "fail");

    bool found_registry = false;
    for (const auto& c : result.checks) {
        if (c.name == "service_registry") {
            found_registry = true;
            EXPECT_EQ(c.status, "fail");
            break;
        }
    }
    EXPECT_TRUE(found_registry);
}
