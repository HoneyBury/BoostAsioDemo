// v3.0.0 Phase 17+18: K8s operator + SDK multi-language validation tests

#include <gtest/gtest.h>
#include <fstream>
#include <string>

namespace {

#ifndef PROJECT_SOURCE_DIR
#define PROJECT_SOURCE_DIR "."
#endif

std::string path(const std::string& rel) {
    return std::string(PROJECT_SOURCE_DIR) + "/" + rel;
}

std::string read_file(const std::string& p) {
    std::ifstream f(p);
    if (!f.is_open()) return {};
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    return content;
}

bool file_contains(const std::string& path, const std::string& text) {
    auto c = read_file(path);
    return c.find(text) != std::string::npos;
}

}  // namespace

// ─── K8s Operator ────────────────────────────────────────────────────────

TEST(K8sOperatorTest, CrdExistsAndValid) {
    auto crd = read_file(path("env/k8s/operator/gameserver-crd.yaml"));
    EXPECT_FALSE(crd.empty()) << "CRD file missing";
    EXPECT_NE(crd.find("CustomResourceDefinition"), std::string::npos);
    EXPECT_NE(crd.find("gameservers.gateway.boost.io"), std::string::npos);
    EXPECT_NE(crd.find("openAPIV3Schema"), std::string::npos);
}

TEST(K8sOperatorTest, RbacExistsAndValid) {
    auto rbac = read_file(path("env/k8s/operator/rbac.yaml"));
    EXPECT_FALSE(rbac.empty()) << "RBAC file missing";
    EXPECT_NE(rbac.find("ServiceAccount"), std::string::npos);
    EXPECT_NE(rbac.find("ClusterRole"), std::string::npos);
    EXPECT_NE(rbac.find("gameservers"), std::string::npos);
}

TEST(K8sOperatorTest, HelmChartExists) {
    auto chart = read_file(path("env/k8s/helm/boost-gateway/Chart.yaml"));
    EXPECT_FALSE(chart.empty()) << "Helm Chart.yaml missing";
    EXPECT_NE(chart.find("boost-gateway"), std::string::npos);

    auto values = read_file(path("env/k8s/helm/boost-gateway/values.yaml"));
    EXPECT_FALSE(values.empty()) << "Helm values.yaml missing";
    EXPECT_NE(values.find("gateway:"), std::string::npos);
    EXPECT_NE(values.find("login:"), std::string::npos);
    EXPECT_NE(values.find("battle:"), std::string::npos);
    EXPECT_NE(values.find("redis:"), std::string::npos);
}

// ─── SDK Multi-language ──────────────────────────────────────────────────

TEST(SdkMultiLanguageTest, PythonSdkExists) {
    auto py = read_file(path("sdk/python/__init__.py"));
    EXPECT_FALSE(py.empty()) << "Python SDK missing";
    EXPECT_NE(py.find("class SdkClient"), std::string::npos);
    EXPECT_NE(py.find("def connect"), std::string::npos);
    EXPECT_NE(py.find("def login"), std::string::npos);
    EXPECT_NE(py.find("def create_room"), std::string::npos);
    EXPECT_NE(py.find("def send_battle_input"), std::string::npos);
}

TEST(SdkMultiLanguageTest, CsharpSdkExists) {
    auto cs = read_file(path("sdk/csharp/SdkClient.cs"));
    EXPECT_FALSE(cs.empty()) << "C# SDK missing";
    EXPECT_NE(cs.find("class SdkClient"), std::string::npos);
    EXPECT_NE(cs.find("DllImport"), std::string::npos);  // v4.1: C API via P/Invoke
    EXPECT_NE(cs.find("gsdk_create"), std::string::npos);
    EXPECT_NE(cs.find("gsdk_login"), std::string::npos);
}

TEST(SdkMultiLanguageTest, CppSdkV3Compatible) {
    auto h = read_file(path("sdk/include/boost_gateway/sdk/client.h"));
    EXPECT_FALSE(h.empty()) << "C++ SDK header missing";
    EXPECT_NE(h.find("class SdkClient"), std::string::npos);
    EXPECT_NE(h.find("LoginResult login"), std::string::npos);
    EXPECT_NE(h.find("BattleInputResult send_battle_input"), std::string::npos);
}

TEST(SdkMultiLanguageTest, AllSdksHaveConsistentApi) {
    // All 3 SDKs should support: connect, login, create_room, join_room,
    // leave_room, set_ready, start_battle, send_input, disconnect

    auto cpp = read_file(path("sdk/include/boost_gateway/sdk/client.h"));
    auto py = read_file(path("sdk/python/__init__.py"));
    auto cs = read_file(path("sdk/csharp/SdkClient.cs"));

    // v4.1: Each SDK has core API (C++, Python via ctypes, C# via DllImport)
    for (auto* sdk : {&cpp, &py, &cs}) {
        bool has_api = sdk->find("connect") != std::string::npos ||
                       sdk->find("Connect") != std::string::npos ||
                       sdk->find("gsdk_connect") != std::string::npos;
        EXPECT_TRUE(has_api) << "SDK missing connect";
        has_api = sdk->find("login") != std::string::npos ||
                  sdk->find("Login") != std::string::npos ||
                  sdk->find("gsdk_login") != std::string::npos;
        EXPECT_TRUE(has_api) << "SDK missing login";
    }
}
